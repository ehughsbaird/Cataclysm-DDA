#include "game.h"
#include "avatar.h"
#include "messages.h"
#include "overmap.h"
#include "overmapbuffer.h"

void game::update_overmap_seen()
{
    // Earth radius in meters
    constexpr double earth_radius = 6'371'000;
    // How high a z-level is in meters
    constexpr double z_level_factor = 3.5;
    // How long a overmap tile is in meters
    constexpr double omt_length = 2 * SEEX;
    // Player height (above z=0) in meters
    const double player_height = ( u.base_height() / 100.0 ) + ( u.pos().z * z_level_factor );

    // How far you can see the ground in meters
    const double ground_sight_meters = std::sqrt( ( player_height * player_height ) +
                                       ( 2 * player_height * earth_radius ) );
    // How far you can see the ground in overmap tiles
    const int ground_sight = ( ground_sight_meters / omt_length ) * 0.1;

    add_msg( m_good, "Height: %.2fm - can see %.2fm (%d tiles)",
             player_height, ground_sight_meters, ground_sight );

    const tripoint_abs_omt ompos = u.global_omt_location();
    //const int base_sight = u.overmap_sight_range( light_level( u.posz() ) );
    // We can always see where we're standing
    overmap_buffer.set_seen( ompos, om_vision_level::full );

    tripoint_range<tripoint_abs_omt> candidates = points_in_radius( ompos, ground_sight );
    /*
    const int x_range = ( candidates.max().x() - candidates.min().x() ) + 1;
    const int y_range = ( candidates.max().y() - candidates.min().y() ) + 1;
    std::vector<float> sightmap( x_range * y_range, 0.f );
    const auto point_to_idx = [&candidates, y_range]( const tripoint_abs_omt & p ) {
        int y = p.y() - candidates.min().y();
        int x = p.x() - candidates.min().x();
        return ( y * y_range ) + x;
    };
    */
    const auto set_seen = []( const tripoint_abs_omt & p, om_vision_level level ) {
        tripoint_abs_omt seen( p );
        do {
            overmap_buffer.set_seen( seen, level );
            --seen.z();
        } while( seen.z() >= 0 );
    };
    std::unordered_set<tripoint_abs_omt> all_opts;
    for( const tripoint_abs_omt &p : candidates ) {
        const point_rel_omt delta = p.xy() - ompos.xy();
        const int h_squared = delta.x() * delta.x() + delta.y() * delta.y();
        if( trigdist && h_squared > ground_sight * ground_sight ) {
            continue;
        }
        all_opts.emplace( p );
    }
    std::vector<tripoint_abs_omt> corners;
    for( const tripoint_abs_omt &p : all_opts ) {
        for( const tripoint_abs_omt &neighbor : points_in_radius( p, 1 ) ) {
            if( all_opts.find( neighbor ) == all_opts.end() ) {
                corners.push_back( p );
                break;
            }
        }
    }
    for( const tripoint_abs_omt &p : corners ) {
        const std::vector<tripoint_abs_omt> line = line_to( ompos, p );
        double sight_points = 1;
        for( auto it = line.begin();
             it != line.end(); ++it ) {
            const oter_id &ter = overmap_buffer.ter( *it );
            sight_points *= std::clamp( 1.0 - ter->get_see_cost(), 0.0, 1.0 );
            if( sight_points > 0.4 ) {
                set_seen( *it, om_vision_level::outlines );
            } else if( sight_points > 0.8 ) {
                set_seen( *it, om_vision_level::details );
            } else if( sight_points > 0.95 ) {
                set_seen( *it, om_vision_level::full );
            }
            set_seen( *it, om_vision_level::vague );
            if( sight_points < 0.01 ) {
                break;
            }
        }
        //sightmap[point_to_idx( p )] = sight_points;
    }
    /*
    int lasty = candidates.min().y();
    FILE *fp = fopen( "out.map", "w" );
    for( const tripoint_abs_omt &p : candidates ) {
        const float sight_points = sightmap[point_to_idx( p )];
        fprintf( fp, " %2.2f", sight_points );
        if( p.y() != lasty ) {
            lasty = p.y();
            fprintf( fp, "\n" );
        }
        if( sight_points <= 0.005 ) {
            continue;
        }
        int tiles_from = rl_dist( p, ompos );
        if( tiles_from < std::floor( base_sight / 2 ) ) {
            set_seen( p, om_vision_level::full );
        } else if( tiles_from < base_sight ) {
            set_seen( p, om_vision_level::details );
        } else if( tiles_from < base_sight * 2 ) {
            set_seen( p, om_vision_level::outlines );
        } else {
            set_seen( p, om_vision_level::vague );
        }
    }
    fclose( fp );
    */
}
