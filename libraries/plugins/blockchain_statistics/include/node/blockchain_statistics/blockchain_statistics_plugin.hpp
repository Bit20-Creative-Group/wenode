#pragma once
#include <node/app/plugin.hpp>

#include <node/chain/node_object_types.hpp>

#include <boost/multi_index/composite_key.hpp>

//
// Plugins should #define their SPACE_ID's so plugins with
// conflicting SPACE_ID assignments can be compiled into the
// same binary (by simply re-assigning some of the conflicting #defined
// SPACE_ID's in a build script).
//
// Assignment of SPACE_ID's cannot be done at run-time because
// various template automagic depends on them being known at compile
// time.
//
#ifndef BLOCKCHAIN_STATISTICS_SPACE_ID
#define BLOCKCHAIN_STATISTICS_SPACE_ID 9
#endif

#ifndef BLOCKCHAIN_STATISTICS_PLUGIN_NAME
#define BLOCKCHAIN_STATISTICS_PLUGIN_NAME "chain_stats"
#endif

namespace node { namespace blockchain_statistics {

using namespace node::chain;
using app::application;

enum blockchain_statistics_object_type
{
   bucket_object_type = ( BLOCKCHAIN_STATISTICS_SPACE_ID << 8 )
};

namespace detail
{
   class blockchain_statistics_plugin_impl;
}

class blockchain_statistics_plugin : public node::app::plugin
{
   public:
      blockchain_statistics_plugin( application* app );
      virtual ~blockchain_statistics_plugin();

      virtual std::string plugin_name()const override { return BLOCKCHAIN_STATISTICS_PLUGIN_NAME; }
      virtual void plugin_set_program_options(
         boost::program_options::options_description& cli,
         boost::program_options::options_description& cfg ) override;
      virtual void plugin_initialize( const boost::program_options::variables_map& options ) override;
      virtual void plugin_startup() override;

      const flat_set< uint32_t >& get_tracked_buckets() const;
      uint32_t get_max_history_per_bucket() const;

   private:
      friend class detail::blockchain_statistics_plugin_impl;
      std::unique_ptr< detail::blockchain_statistics_plugin_impl > _my;
};

struct bucket_object : public object< bucket_object_type, bucket_object >
{
   template< typename Constructor, typename Allocator >
   bucket_object( Constructor&& c, allocator< Allocator > a )
   {
      c( *this );
   }

   id_type              id;

   fc::time_point   open;                                        ///< Open time of the bucket
   uint32_t             seconds = 0;                                 ///< Seconds accounted for in the bucket
   uint32_t             blocks = 0;                                  ///< Blocks produced
   uint32_t             bandwidth = 0;                               ///< Bandwidth in bytes
   uint32_t             operations = 0;                              ///< Operations evaluated
   uint32_t             transactions = 0;                            ///< Transactions processed
   uint32_t             transfers = 0;                               ///< Account to account transfers
   share_type           assets_transferred = 0;                      ///< assets transferred from account to account
   share_type           USD_transferred = 0;                         ///< USD transferred from account to account
   share_type           USD_paid_as_interest = 0;                    ///< USD paid as interest
   uint32_t             paid_accounts_created = 0;                   ///< Accounts created with fee
   uint32_t             mined_accounts_created = 0;                  ///< Accounts mined for free
   uint32_t             root_comments = 0;                           ///< Top level root comments
   uint32_t             root_comment_edits = 0;                      ///< Edits to root comments
   uint32_t             root_comments_deleted = 0;                   ///< Root comments deleted
   uint32_t             replies = 0;                                 ///< Replies to comments
   uint32_t             reply_edits = 0;                             ///< Edits to replies
   uint32_t             replies_deleted = 0;                         ///< Replies deleted
   uint32_t             new_root_votes = 0;                          ///< New votes on root comments
   uint32_t             changed_root_votes = 0;                      ///< Changed votes on root comments
   uint32_t             new_reply_votes = 0;                         ///< New votes on replies
   uint32_t             changed_reply_votes = 0;                     ///< Changed votes on replies
   uint32_t             payouts = 0;                                 ///< Number of comment payouts
   share_type           USD_paid_to_authors = 0;                     ///< Amount of USD paid to authors
   share_type           rewards_paid_to_authors = 0;                   ///< Amount of VESS paid to authors
   share_type           rewards_paid_to_curators = 0;                  ///< Amount of rewards paid to curators
   share_type           liquidity_rewards_paid = 0;                  ///< Amount of assets paid to market makers
   uint32_t             asset_stake_transfers = 0;                    ///< Transfers of 
   share_type           asset_stake_value = 0;                            ///< Amount of staked assets
   uint32_t             asset_unstake_transfers = 0;                   ///< New asset unstake withdrawal requests
   uint32_t             asset_unstake_adjustments = 0;                ///< Changes to fund withdrawal requests
   share_type           asset_unstake_rate_total = 0;
   uint32_t             asset_unstake_withdrawals = 0;                 ///< Number of fund withdrawals
   uint32_t             asset_unstake_completed = 0;                  ///< Processed fund withdrawals that are now finished
   share_type           total_assets_unstaked = 0;                         ///< Amount withdrawn
   share_type           total_stake_transferred = 0;                       ///< Amount of stake transferred to another account
   uint32_t             limit_orders_created = 0;                    ///< Limit orders created
   uint32_t             limit_orders_filled = 0;                     ///< Limit orders filled
   uint32_t             limit_orders_cancelled = 0;                  ///< Limit orders cancelled
   uint32_t             total_pow = 0;                               ///< POW submitted
   uint128_t            estimated_hashpower = 0;                     ///< Estimated average hashpower over interval
};

typedef oid< bucket_object > bucket_id_type;

struct by_id;
struct by_bucket;
typedef multi_index_container<
   bucket_object,
   indexed_by<
      ordered_unique< tag< by_id >, member< bucket_object, bucket_id_type, &bucket_object::id > >,
      ordered_unique< tag< by_bucket >,
         composite_key< bucket_object,
            member< bucket_object, uint32_t, &bucket_object::seconds >,
            member< bucket_object, fc::time_point, &bucket_object::open >
         >
      >
   >,
   allocator< bucket_object >
> bucket_index;

} } // node::blockchain_statistics

FC_REFLECT( node::blockchain_statistics::bucket_object,
   (id)
   (open)
   (seconds)
   (blocks)
   (bandwidth)
   (operations)
   (transactions)
   (transfers)
   (assets_transferred)
   (USD_transferred)
   (USD_paid_as_interest)
   (paid_accounts_created)
   (mined_accounts_created)
   (root_comments)
   (root_comment_edits)
   (root_comments_deleted)
   (replies)
   (reply_edits)
   (replies_deleted)
   (new_root_votes)
   (changed_root_votes)
   (new_reply_votes)
   (changed_reply_votes)
   (payouts)
   (rewards_paid_to_authors)
   (rewards_paid_to_curators)
   (asset_stake_transfers)
   (asset_stake_value)
   (asset_unstake_transfers)
   (asset_unstake_adjustments)
   (asset_unstake_rate_total)
   (asset_unstake_withdrawals)
   (asset_unstake_completed)
   (total_assets_unstaked)
   (total_stake_transferred)
   (limit_orders_created)
   (limit_orders_filled)
   (limit_orders_cancelled)
   (total_pow)
   (estimated_hashpower)
)
CHAINBASE_SET_INDEX_TYPE( node::blockchain_statistics::bucket_object, node::blockchain_statistics::bucket_index )
