#include "avatar.h"
#include "game.h"
#include "omdata.h"
#include "overmap.h"
#include "overmapbuffer.h"

void game::update_overmap_seen()
{
    const tripoint_abs_omt ompos = u.pos_abs_omt();
    const int dist = u.overmap_modified_sight_range( light_level( u.posz() ) );
    const int dist_squared = dist * dist;
    // We can always see where we're standing
    overmap_buffer.set_seen( ompos, om_vision_level::full );
    for( const tripoint_abs_omt &p : points_in_radius( ompos, dist ) ) {
        const point_rel_omt delta = p.xy() - ompos.xy();
        const int h_squared = delta.x() * delta.x() + delta.y() * delta.y();
        if( trigdist && h_squared > dist_squared ) {
            continue;
        }
        if( delta == point_rel_omt() ) {
            // 1. This case is already handled outside of the loop
            // 2. Calculating multiplier would cause division by zero
            continue;
        }
        // If circular distances are enabled, scale overmap distances by the diagonality of the sight line.
        point_rel_omt abs_delta = delta.abs();
        int max_delta = std::max( abs_delta.x(), abs_delta.y() );
        const float multiplier = trigdist ? std::sqrt( h_squared ) / max_delta : 1;
        const std::vector<tripoint_abs_omt> line = line_to( ompos, p );
        float sight_points = dist;
        for( auto it = line.begin();
             it != line.end() && sight_points >= 0; ++it ) {
            const oter_id &ter = overmap_buffer.ter( *it );
            sight_points -= static_cast<int>( ter->get_see_cost() ) * multiplier;
        }
        if( sight_points < 0 ) {
            continue;
        }
        const auto set_seen = []( const tripoint_abs_omt & p, om_vision_level level ) {
            tripoint_abs_omt seen( p );
            do {
                overmap_buffer.set_seen( seen, level );
                --seen.z();
            } while( seen.z() >= 0 );
        };
        int tiles_from = rl_dist( p, ompos );
        if( tiles_from < std::floor( dist / 2 ) ) {
            set_seen( p, om_vision_level::full );
        } else if( tiles_from < dist ) {
            set_seen( p, om_vision_level::details );
        } else if( tiles_from < dist * 2 ) {
            set_seen( p, om_vision_level::outlines );
        } else {
            set_seen( p, om_vision_level::vague );
        }
    }
}
