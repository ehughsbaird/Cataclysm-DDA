#include "avatar.h"
#include "game.h"
#include "omcasting.h"
#include "omdata.h"
#include "overmap.h"
#include "overmapbuffer.h"

std::vector<tripoint_abs_omt> edges_around( const tripoint_abs_omt &center, int dist )
{
    std::vector<tripoint_abs_omt> ret;
    tripoint_range<tripoint_abs_omt> candidates = points_in_radius( center, dist );

    std::unordered_set<tripoint_abs_omt> all_opts;
    for( const tripoint_abs_omt &p : candidates ) {
        const point_rel_omt delta = p.xy() - center.xy();
        const int h_squared = delta.x() * delta.x() + delta.y() * delta.y();
        if( trigdist && h_squared > dist * dist ) {
            continue;
        }
        all_opts.emplace( p );
    }

    for( const tripoint_abs_omt &p : all_opts ) {
        for( const tripoint_abs_omt &neighbor : points_in_radius( p, 1 ) ) {
            if( all_opts.find( neighbor ) == all_opts.end() ) {
                ret.push_back( p );
                break;
            }
        }
    }
    return ret;
}

void view_overmap_line( const tripoint_abs_omt &from, const tripoint_abs_omt &to,
                        std::unordered_map<tripoint_abs_omt, oter_id> &seen_tiles )
{
    double vision_level = 1.0;
    // draw a line from the point we're standing to each point that would be the furthest point we can see
    const std::vector<tripoint_abs_omt> line = line_to( from, to );
    int last_z = from.z();
    bool could_see_vertical_from_last = overmap_buffer.ter( from )->can_see_down_through();
    for( const tripoint_abs_omt &pt : line ) {
        oter_id ter;
        auto iter = seen_tiles.find( pt );
        if( iter == seen_tiles.end() ) {
            ter = overmap_buffer.ter( pt );
            seen_tiles[pt] = ter;
        } else {
            ter = iter->second;
        }
        // looking through the ground
        if( pt.z() < last_z && !could_see_vertical_from_last ) {
            break;
        }
        // looking up
        if( pt.z() > last_z && !ter->can_see_down_through() ) {
            break;
        }
        if( vision_level > 0.9 ) {
            overmap_buffer.set_seen( pt, om_vision_level::full );
        } else if( vision_level > 0.7 ) {
            overmap_buffer.set_seen( pt, om_vision_level::details );
        } else if( vision_level > 0.4 ) {
            overmap_buffer.set_seen( pt, om_vision_level::outlines );
        } else {
            overmap_buffer.set_seen( pt, om_vision_level::vague );
        }
        vision_level *= ( 1.0 - ter->get_see_cost() );

        last_z = pt.z();
        could_see_vertical_from_last = ter->can_see_down_through();

        if( vision_level < 0.01 ) {
            break;
        }
    }
}

// called when shifting the map, changing z-levels, generally moving the player
void game::update_overmap_seen()
{
    // assumptions:
    // z level 0 is "sea level"
    // each tile is 4m tall
    constexpr int Z_LEVEL_HEIGHT = 4;
    // each overmap tile is SEEX * 2 m wide
    constexpr int TILE_WIDTH = SEEX * 2;
    // The world is a perfect sphere with no elevation
    // The player is making no particular effort to scout
    // - They can only see a portion of the way to the horizon
    constexpr int HORIZON_DISTANCE_DIVIDER = 4;
    // - Only things that are particularly close are taken in with a high level of detail

    // in meters
    constexpr double EARTH_RADIUS = 6'371'000;

    // eye height above "sea level"
    // player height in meters, 90% for eye location plus z level height
    // because players cannot be 4 meters tall, if this is negative, you're underground
    const double player_eye_height = ( ( u.height() / 100.0 ) * 0.9 ) +
                                     u.pos_abs_omt().z() * Z_LEVEL_HEIGHT;

    const double distance_to_horizon = u.pos_abs_omt().z() < 0 ? HORIZON_DISTANCE_DIVIDER :
                                       std::sqrt(
                                           ( ( player_eye_height + EARTH_RADIUS ) * ( player_eye_height + EARTH_RADIUS ) )
                                           - ( EARTH_RADIUS * EARTH_RADIUS ) );

    const int sight_radius = std::min<int>( distance_to_horizon / ( HORIZON_DISTANCE_DIVIDER *
                                            TILE_WIDTH ),
                                            ( OMAPX / 2 ) - 1 );
    printf( "%d\n", sight_radius );

    const tripoint_abs_omt ompos = u.pos_abs_omt();
    // We can always see where we're standing
    overmap_buffer.set_seen( ompos, om_vision_level::full );

    std::array<cata::mdarray<float, point_rel_omt, OMAPX, OMAPY>, OVERMAP_LAYERS> output;
    std::array<cata::mdarray<float, point_rel_omt, OMAPX, OMAPY>, OVERMAP_LAYERS> input;
    std::array<cata::mdarray<bool, point_rel_omt, OMAPX, OMAPY>, OVERMAP_LAYERS> floors;
    for( int z = 0; z < OVERMAP_LAYERS; ++z ) {
        input[z].fill( 0.f );
        floors[z].fill( false );
        output[z].fill( 0.f );
        for( int x = -sight_radius; x <= sight_radius; ++x ) {
            for( int y = -sight_radius; y <= sight_radius; ++y ) {
                point_rel_omt index( x + sight_radius, y + sight_radius );
                tripoint_abs_omt loc( ompos.x() + x, ompos.y() + y, z );
                oter_id ter = overmap_buffer.ter( loc );
                input[z][index] = ter->get_see_cost();
                floors[z][index] = ter->can_see_down_through();
            }
        }
    }

    std::array<cata::mdarray<float, point_rel_omt, OMAPX, OMAPY> *, OVERMAP_LAYERS> output_caches;
    std::array<const cata::mdarray<float, point_rel_omt, OMAPX, OMAPY> *, OVERMAP_LAYERS> input_arrays;
    std::array<const cata::mdarray<bool, point_rel_omt, OMAPX, OMAPY> *, OVERMAP_LAYERS> floor_caches;
    for( int z = 0; z < OVERMAP_LAYERS; ++z ) {
        output_caches[z] = &output[z];
        input_arrays[z] = &input[z];
        floor_caches[z] = &floors[z];
    }

    omcast::cast_zlight<float, omcast::sight_calc, omcast::sight_check, omcast::accumulate_transparency>
    ( output_caches,
      input_arrays, floor_caches, tripoint_rel_omt( sight_radius, sight_radius, ompos.z() ), 0, 1.f,
      omcast::vertical_direction::BOTH );

    FILE *fp = fopen( "draw.log", "w" );
    for( int z = 0; z < OVERMAP_LAYERS; ++z ) {
        float min = 19819;
        float max = -18919;
        fprintf( fp, "Z: %d\n", z - OVERMAP_DEPTH );
        for( int x = 0; x < OMAPX; ++x ) {
            for( int y = 0; y < OMAPY; ++y ) {
                float val = ( *output_caches[z] )[x][y];
                min = std::min( val, min );
                max = std::max( val, max );
                int bucket = static_cast<int>( 9 * std::clamp( val, 0.f, 1.f ) );
                const char *color = "\033[30m";
                if( bucket > 0 ) {
                    color = "\033[32m";
                }
                fprintf( fp, "%s%c\033[0m", color, '0' + bucket );
            }
            fprintf( fp, "\n" );
        }
        fprintf( fp, "Min: %g, Max: %g\n\n", min, max );
    }
    fclose( fp );
}
