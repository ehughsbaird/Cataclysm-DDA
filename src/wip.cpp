#include "game.h"
#include "avatar.h"
#include "messages.h"
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

void game::update_overmap_seen()
{
    // Earth radius in meters
    constexpr double earth_radius = 6'371'000;
    // How high a z-level is in meters
    constexpr double z_level_factor = 3.5;
    // fduge factor
    constexpr double z_level_fudge = 0.1;
    // How long a overmap tile is in meters
    constexpr double omt_length = 2 * SEEX;
    // Player height (above z=0) in meters
    const double player_height = ( u.base_height() / 100.0 ) +
                                 std::max( 0.0, ( u.pos().z * z_level_factor ) * z_level_fudge );

    // How far you can see the ground in meters
    const double ground_sight_meters = std::sqrt( ( player_height * player_height ) +
                                       ( 2 * player_height * earth_radius ) );
    // How far you can see the ground in overmap tiles
    const int ground_sight = ( ground_sight_meters / omt_length ) /
                             get_option<int>( "OVERMAP_SIGHT_DISTANCE_DIVISOR" );

    const int mod_sight = u.overmap_modified_sight_range( light_level( u.posz() ) );
    const int base_sight = u.overmap_sight_range( light_level( u.posz() ) );

    const tripoint_abs_omt ompos = u.global_omt_location();
    //const int base_sight = u.overmap_sight_range( light_level( u.posz() ) );
    // We can always see where we're standing
    overmap_buffer.set_seen( ompos, om_vision_level::full );

    std::vector<tripoint_abs_omt> corners = edges_around( ompos, ground_sight );
    const auto set_seen = []( const tripoint_abs_omt & p, om_vision_level level ) {
        tripoint_abs_omt seen( p );
        while( true ) {
            overmap_buffer.set_seen( seen, level );
            if( !overmap_buffer.ter( seen )->can_see_down_through() ) {
                break;
            }
            --seen.z();
        }
    };
    add_msg_debug( debugmode::DF_OVERMAP, "Vision: drawing %d lines for at most %d tiles\n",
                   corners.size(), ground_sight );
    add_msg_debug( debugmode::DF_OVERMAP, "Sight points: %d/%d (base/modified)\n",
                   base_sight, mod_sight );
    add_msg_debug( debugmode::DF_OVERMAP, "Cost for full, details, outlines: %g, %g, %g\n",
                   std::floor( base_sight * 0.9 ), std::ceil( base_sight * 0.75 ), std::ceil( base_sight * 0.2 ) );
    for( const tripoint_abs_omt &p : corners ) {
        const std::vector<tripoint_abs_omt> line = line_to( ompos, p );
        int points = mod_sight;
        int dist = 0;
        for( auto it = line.begin();
             it != line.end(); ++it ) {
            const oter_id &ter = overmap_buffer.ter( *it );
            int dist_penalty = dist * 0.2;
            // ends after ~.5km at most
            if( points - dist_penalty > std::floor( base_sight * 0.9 ) && dist < ( 500 / omt_length ) ) {
                set_seen( *it, om_vision_level::full );
                // ends after ~1km at most
            } else if( points - dist_penalty > std::ceil( base_sight * 0.75 ) &&
                       dist < ( 1000 / omt_length ) ) {
                set_seen( *it, om_vision_level::details );
            } else if( points - dist_penalty > std::ceil( base_sight * 0.2 ) ) {
                set_seen( *it, om_vision_level::outlines );
            } else {
                set_seen( *it, om_vision_level::vague );
            }
            points -= ter->get_see_cost();
            ++dist;
            if( points <= 0 ) {
                break;
            }
        }
    }
}
