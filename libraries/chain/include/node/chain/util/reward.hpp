#pragma once

#include <node/chain/util/asset.hpp>
#include <node/chain/node_objects.hpp>

#include <node/protocol/asset.hpp>
#include <node/protocol/config.hpp>
#include <node/protocol/types.hpp>

#include <fc/reflect/reflect.hpp>

#include <fc/uint128.hpp>

namespace node { namespace chain { namespace util {

using node::protocol::asset;
using node::protocol::price;
using node::protocol::share_type;

using fc::uint128_t;

struct comment_reward_context
{
   share_type ESCORreward;
   uint16_t   reward_weight = 0;
   asset      max_EUSD;
   uint128_t  total_ESCORreward2;
   asset      total_reward_fund_ECO;
   price      current_ECO_price;
   curve_id   reward_curve = quadratic;
   uint128_t  content_constant = CONTENT_CONSTANT_HF0;
};

uint64_t get_ESCOR_reward( const comment_reward_context& ctx );

inline uint128_t get_content_constant_s()
{
   return CONTENT_CONSTANT_HF0; // looking good for posters
}

uint128_t evaluate_reward_curve( const uint128_t& ESCORreward, const curve_id& curve = quadratic, const uint128_t& content_constant = CONTENT_CONSTANT_HF0 );

inline bool is_comment_payout_dust( const price& p, uint64_t ECOpayout )
{
   return to_EUSD( p, asset( ECOpayout, SYMBOL_ECO ) ) < MIN_PAYOUT_EUSD;
}

} } } // node::chain::util

FC_REFLECT( node::chain::util::comment_reward_context,
   (ESCORreward)
   (reward_weight)
   (max_EUSD)
   (total_ESCORreward2)
   (total_reward_fund_ECO)
   (current_ECO_price)
   (reward_curve)
   (content_constant)
   )