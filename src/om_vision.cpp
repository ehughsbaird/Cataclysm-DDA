#include "avatar.h"
#include "game.h"
#include "omdata.h"
#include "overmap.h"
#include "overmapbuffer.h"

static std::vector<tripoint_abs_omt> edges_around( const tripoint_abs_omt &center, int dist )
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

static void view_overmap_line( const tripoint_abs_omt &from, const tripoint_abs_omt &to,
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
        vision_level *= ( 1.0 - ter->get_see_cost() );
        overmap_buffer.set_seen( pt, om_vision_level::full );

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

    const int sight_radius = distance_to_horizon / ( HORIZON_DISTANCE_DIVIDER * TILE_WIDTH );
    fprintf( stderr, "sight radius (%g) -> (%g)^2 - %g^2 -> %g - %g -> sqrt(%g) ->%g, %d\n",
             player_eye_height, player_eye_height + EARTH_RADIUS, EARTH_RADIUS,
             ( ( player_eye_height + EARTH_RADIUS ) * ( player_eye_height * EARTH_RADIUS ) ),
             EARTH_RADIUS * EARTH_RADIUS, ( ( player_eye_height + EARTH_RADIUS ) *
                                            ( player_eye_height * EARTH_RADIUS ) ) - ( EARTH_RADIUS * EARTH_RADIUS ), distance_to_horizon,
             sight_radius );
    fflush( stderr );

    const tripoint_abs_omt ompos = u.pos_abs_omt();
    // We can always see where we're standing
    overmap_buffer.set_seen( ompos, om_vision_level::full );

    std::unordered_map<tripoint_abs_omt, oter_id> seen_tiles_acc;

    for( int z = OVERMAP_HEIGHT; z >= -OVERMAP_DEPTH; --z ) {
        std::vector<tripoint_abs_omt> ends;
        if( z == OVERMAP_HEIGHT || z == OVERMAP_DEPTH ) {
            tripoint_range<tripoint_abs_omt> points = points_in_radius( tripoint_abs_omt( ompos.xy(), z ),
                    sight_radius );
            ends.reserve( points.size() );
            for( const tripoint_abs_omt &edge : points ) {
                ends.emplace_back( edge );
            }
        } else {
            ends = edges_around( tripoint_abs_omt( ompos.xy(), z ), sight_radius );
        }
        for( const tripoint_abs_omt &edge : ends ) {
            view_overmap_line( ompos, tripoint_abs_omt( edge.xy(), z ), seen_tiles_acc );
        }
    }
}
