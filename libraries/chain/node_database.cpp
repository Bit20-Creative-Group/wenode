#include <node/protocol/types.hpp>
#include <node/protocol/authority.hpp>
#include <node/protocol/transaction.hpp>
#include <node/chain/database.hpp>

#include <node/chain/node_object_types.hpp>
#include <node/protocol/node_operations.hpp>
#include <node/chain/block_summary_object.hpp>
#include <node/chain/custom_operation_interpreter.hpp>

#include <node/chain/database_exceptions.hpp>
#include <node/chain/db_with.hpp>
#include <node/chain/evaluator_registry.hpp>
#include <node/chain/global_property_object.hpp>
#include <node/chain/history_object.hpp>
#include <node/chain/index.hpp>
#include <node/chain/node_evaluator.hpp>
#include <node/chain/node_objects.hpp>
#include <node/chain/transaction_object.hpp>
#include <node/chain/shared_db_merkle.hpp>
#include <node/chain/operation_notification.hpp>
#include <node/chain/producer_schedule.hpp>

#include <node/chain/util/asset.hpp>
#include <node/chain/util/reward.hpp>
#include <node/chain/util/uint256.hpp>
#include <node/chain/util/reward.hpp>

#include <fc/smart_ref_impl.hpp>
#include <fc/uint128.hpp>

#include <fc/container/deque.hpp>

#include <fc/io/fstream.hpp>
#include <fc/io/json.hpp>

#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>

namespace node { namespace chain {

//namespace db2 = graphene::db2;

struct object_schema_repr
{
   std::pair< uint16_t, uint16_t > space_type;
   std::string type;
};

struct operation_schema_repr
{
   std::string id;
   std::string type;
};

struct db_schema
{
   std::map< std::string, std::string > types;
   std::vector< object_schema_repr > object_types;
   std::string operation_type;
   std::vector< operation_schema_repr > custom_operation_types;
};

} }

FC_REFLECT( node::chain::object_schema_repr, (space_type)(type) )
FC_REFLECT( node::chain::operation_schema_repr, (id)(type) )
FC_REFLECT( node::chain::db_schema, (types)(object_types)(operation_type)(custom_operation_types) )

namespace node { namespace chain {

using boost::container::flat_set;
using node::protocol::share_type;

class database_impl
{
   public:
      database_impl( database& self );

      database&                              _self;
      evaluator_registry< operation >        _evaluator_registry;
};

database_impl::database_impl( database& self )
   : _self(self), _evaluator_registry(self) {}

database::database()
   : _my( new database_impl(*this) ) {}

database::~database()
{
   clear_pending();
}

void database::open( const fc::path& data_dir, const fc::path& shared_mem_dir, uint64_t shared_file_size, uint32_t chainbase_flags )
{ try {
   chainbase::database::open( shared_mem_dir, chainbase_flags, shared_file_size );
   initialize_indexes();
   initialize_evaluators();

   if( chainbase_flags & chainbase::database::read_write )
   {
      if( !find< dynamic_global_property_object >() )
      {
         with_write_lock( [&]()
         {
            init_genesis();
         });
      }
         
      _block_log.open( data_dir / "block_log" );

      auto log_head = _block_log.head();

      // Rewind all undo state. This should return us to the state at the last irreversible block.
      with_write_lock( [&]()
      {
         undo_all();
         FC_ASSERT( revision() == int64_t( head_block_num() ), 
            "Chainbase revision does not match head block num",
            ("rev", revision())("head_block", head_block_num()) );
         validate_invariants();
      });

      if( head_block_num() )
      {
         auto head_block = _block_log.read_block_by_num( head_block_num() );
         // This assertion should be caught and a reindex should occur
         FC_ASSERT( head_block.valid() && head_block->id() == head_block_id(), 
            "Chain state does not match block log. Please reindex blockchain." );

         _fork_db.start_block( *head_block );
      }
   }

   with_read_lock( [&]()
   {
      init_hardforks(); // Writes to local state, but reads from db
   });
} FC_CAPTURE_LOG_AND_RETHROW( (data_dir)(shared_mem_dir)(shared_file_size) ) }


/**
 * Creates a new blockchain from the genesis block, 
 * creates all necessary network objects.
 * Generates initial assets, accounts, producers, communities
 * and sets initial global dynamic properties.
 */
void database::init_genesis()
{ try {
   struct auth_inhibitor
   {
      auth_inhibitor(database& db) : db(db), old_flags(db.node_properties().skip_flags)
      { db.node_properties().skip_flags |= skip_authority_check; }
      ~auth_inhibitor()
      { db.node_properties().skip_flags = old_flags; }
   private:
      database& db;
      uint32_t old_flags;
   } inhibitor(*this);
   
   // Create the Global Dynamic Properties Object to track consensus critical network and chain information

   time_point now = GENESIS_TIME;

   ilog( "\n" );
   ilog( "======================================================" );
   ilog( "========== INIT GENESIS: STARTING NEW CHAIN ==========" );
   ilog( "====================================================== \n" );

   create< dynamic_global_property_object >( [&]( dynamic_global_property_object& dgpo )
   {
      dgpo.current_producer = GENESIS_ACCOUNT_BASE_NAME;
      dgpo.time = now;
      dgpo.recent_slots_filled = fc::uint128::max_value();
      dgpo.participation_count = 128;
   });

   create< median_chain_property_object >( [&]( median_chain_property_object& p ){});

   create< comment_metrics_object >( [&]( comment_metrics_object& o ) {});

   for( int i = 0; i < 0x10000; i++ )
   {
      create< block_summary_object >( [&]( block_summary_object& ) {});
   }

   create< hardfork_property_object >( [&]( hardfork_property_object& hpo )
   {
      hpo.processed_hardforks.push_back( now );
   });

   // Create the initial Reward fund object to contain the balances of the network reward funds and parameters

   create< reward_fund_object >( [&]( reward_fund_object& rfo )
   {
      rfo.symbol = SYMBOL_COIN;
      rfo.content_reward_balance = asset( 0, SYMBOL_COIN );
      rfo.validation_reward_balance = asset( 0, SYMBOL_COIN );
      rfo.txn_stake_reward_balance = asset( 0, SYMBOL_COIN );
      rfo.work_reward_balance = asset( 0, SYMBOL_COIN );
      rfo.producer_activity_reward_balance = asset( 0, SYMBOL_COIN );
      rfo.supernode_reward_balance = asset( 0, SYMBOL_COIN );
      rfo.power_reward_balance = asset( 0, SYMBOL_COIN );
      rfo.community_fund_balance = asset( 0, SYMBOL_COIN );
      rfo.development_reward_balance = asset( 0, SYMBOL_COIN );
      rfo.marketing_reward_balance = asset( 0, SYMBOL_COIN );
      rfo.advocacy_reward_balance = asset( 0, SYMBOL_COIN );
      rfo.activity_reward_balance = asset( 0, SYMBOL_COIN );
      rfo.premium_partners_fund_balance = asset( 0, SYMBOL_COIN );
      rfo.recent_content_claims = 0;
      rfo.recent_activity_claims = 0;
      rfo.last_updated = now;
   });

   // Create initial blockchain accounts

   create< account_object >( [&]( account_object& a )
   {
      a.name = INIT_ACCOUNT;
      a.registrar = INIT_ACCOUNT;
      a.referrer = INIT_ACCOUNT;
      a.secure_public_key = get_public_key( a.name, "secure", INIT_ACCOUNT_PASSWORD );
      a.connection_public_key = get_public_key( a.name, "connection", INIT_ACCOUNT_PASSWORD );
      a.friend_public_key = get_public_key( a.name, "friend", INIT_ACCOUNT_PASSWORD );
      a.companion_public_key = get_public_key( a.name, "companion", INIT_ACCOUNT_PASSWORD );
      a.created = now;
      a.last_updated = now;
      a.last_vote_time = now;
      a.last_view_time = now;
      a.last_share_time = now;
      a.last_post = now;
      a.last_root_post = now;
      a.last_transfer_time = now;
      a.last_activity_reward = now;
      a.last_account_recovery = now;
      a.last_community_created = now;
      a.last_asset_created = now;
      from_string( a.json, "" );
      from_string( a.json_private, "" );
      from_string( a.details, INIT_DETAILS );
      from_string( a.url, INIT_URL );
      from_string( a.profile_image, INIT_IMAGE );
      a.membership = membership_tier_type::TOP_MEMBERSHIP;
      a.membership_expiration = time_point::maximum();
      a.mined = true;
      a.active = true;
      a.can_vote = true;
      a.revenue_share = true;
   });

   create< account_authority_object >( [&]( account_authority_object& auth )
   {
      auth.account = INIT_ACCOUNT;
      auth.owner_auth.add_authority( get_public_key( INIT_ACCOUNT, "owner", INIT_ACCOUNT_PASSWORD ), 1 );
      auth.owner_auth.weight_threshold = 1;
      auth.active_auth.add_authority( get_public_key( INIT_ACCOUNT, "active", INIT_ACCOUNT_PASSWORD ), 1 );
      auth.active_auth.weight_threshold = 1;
      auth.posting_auth.add_authority( get_public_key( INIT_ACCOUNT, "posting", INIT_ACCOUNT_PASSWORD ), 1 );
      auth.posting_auth.weight_threshold = 1;
   });

   create< account_permission_object >( [&]( account_permission_object& aao )
   {
      aao.account = INIT_ACCOUNT;
   });

   create< account_following_object >( [&]( account_following_object& afo )
   {
      afo.account = INIT_ACCOUNT;
      afo.last_updated = now;
   });

   create< account_object >( [&]( account_object& a )
   {
      a.name = INIT_CEO;
      a.registrar = INIT_ACCOUNT;
      a.referrer = INIT_ACCOUNT;
      a.secure_public_key = get_public_key( a.name, "secure", INIT_ACCOUNT_PASSWORD );
      a.connection_public_key = get_public_key( a.name, "connection", INIT_ACCOUNT_PASSWORD );
      a.friend_public_key = get_public_key( a.name, "friend", INIT_ACCOUNT_PASSWORD );
      a.companion_public_key = get_public_key( a.name, "companion", INIT_ACCOUNT_PASSWORD );
      a.created = now;
      a.last_updated = now;
      a.last_vote_time = now;
      a.last_view_time = now;
      a.last_share_time = now;
      a.last_post = now;
      a.last_root_post = now;
      a.last_transfer_time = now;
      a.last_activity_reward = now;
      a.last_account_recovery = now;
      a.last_community_created = now;
      a.last_asset_created = now;
      from_string( a.json, "" );
      from_string( a.json_private, "" );
      from_string( a.details, INIT_DETAILS );
      from_string( a.url, INIT_URL );
      from_string( a.profile_image, INIT_IMAGE );
      a.membership = membership_tier_type::TOP_MEMBERSHIP;
      a.membership_expiration = time_point::maximum();
      a.mined = true;
      a.active = true;
      a.can_vote = true;
      a.revenue_share = false;
   });

   create< account_authority_object >( [&]( account_authority_object& auth )
   {
      auth.account = INIT_CEO;
      auth.owner_auth.add_authority( get_public_key( INIT_CEO, "owner", INIT_ACCOUNT_PASSWORD ), 1 );
      auth.owner_auth.weight_threshold = 1;
      auth.active_auth.add_authority( get_public_key( INIT_CEO, "active", INIT_ACCOUNT_PASSWORD ), 1 );
      auth.active_auth.weight_threshold = 1;
      auth.posting_auth.add_authority( get_public_key( INIT_CEO, "posting", INIT_ACCOUNT_PASSWORD ), 1 );
      auth.posting_auth.weight_threshold = 1;
   });

   create< account_permission_object >( [&]( account_permission_object& aao )
   {
      aao.account = INIT_CEO;
   });

   create< account_following_object >( [&]( account_following_object& afo ) 
   {
      afo.account = INIT_CEO;
      afo.last_updated = now;
   });

   create< account_business_object >( [&]( account_business_object& abo )
   {
      abo.account = INIT_ACCOUNT;
      abo.business_type = business_structure_type::PUBLIC_BUSINESS;
      abo.executive_board.CHIEF_EXECUTIVE_OFFICER = INIT_CEO;
      abo.officer_vote_threshold = 1000 * BLOCKCHAIN_PRECISION;
      abo.business_public_key = get_public_key( abo.account, "business", INIT_ACCOUNT_PASSWORD );
      abo.members.insert( INIT_CEO );
      abo.officers.insert( INIT_CEO );
      abo.executives.insert( INIT_CEO );
      abo.equity_assets.insert( SYMBOL_EQUITY );
      abo.equity_revenue_shares[ SYMBOL_EQUITY ] = DIVIDEND_SHARE_PERCENT;
      abo.credit_assets.insert( SYMBOL_CREDIT );
      abo.credit_revenue_shares[ SYMBOL_CREDIT ] = BUYBACK_SHARE_PERCENT;
      abo.active = true;
      abo.created = now;
      abo.last_updated = now;
   });

   create< account_officer_vote_object >( [&]( account_officer_vote_object& aovo )
   {
      aovo.account = INIT_CEO;
      aovo.business_account = INIT_ACCOUNT;
      aovo.officer_account = INIT_CEO;
      aovo.vote_rank = 1;
   });

   create< account_executive_vote_object >( [&]( account_executive_vote_object& aevo )
   {
      aevo.account = INIT_CEO;
      aevo.business_account = INIT_ACCOUNT;
      aevo.executive_account = INIT_CEO;
      aevo.role = executive_role_type::CHIEF_EXECUTIVE_OFFICER;
      aevo.vote_rank = 1;
   });

   create< governance_account_object >( [&]( governance_account_object& gao )
   {
      gao.account = INIT_ACCOUNT;
      from_string( gao.url, INIT_URL );
      from_string( gao.details, INIT_DETAILS );
      gao.created = now;
      gao.active = true;
   });

   create< supernode_object >( [&]( supernode_object& s )
   {
      s.account = INIT_ACCOUNT;
      from_string( s.url, INIT_URL );
      from_string( s.details, INIT_DETAILS );
      from_string( s.node_api_endpoint, INIT_NODE_ENDPOINT );
      from_string( s.auth_api_endpoint, INIT_AUTH_ENDPOINT );
      from_string( s.notification_api_endpoint, INIT_NOTIFICATION_ENDPOINT );
      from_string( s.ipfs_endpoint, INIT_IPFS_ENDPOINT );
      from_string( s.bittorrent_endpoint, INIT_BITTORRENT_ENDPOINT );
      s.active = true;
      s.created = now;
      s.last_updated = now;
      s.last_activation_time = now;
   });

   create< network_officer_object >( [&]( network_officer_object& noo )
   {
      noo.account = INIT_CEO;
      noo.officer_type = network_officer_role_type::DEVELOPMENT;
      from_string( noo.url, INIT_URL );
      from_string( noo.details, INIT_DETAILS );
      noo.officer_approved = true;
      noo.created = now;
      noo.active = true;
   });

   create< executive_board_object >( [&]( executive_board_object& ebo )
   {
      ebo.account = INIT_ACCOUNT;
      ebo.budget = asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT );
      from_string( ebo.url, INIT_URL );
      from_string( ebo.details, INIT_DETAILS );
      ebo.active = true;
      ebo.created = now;
      ebo.board_approved = true;
   });

   create< interface_object >( [&]( interface_object& i )
   {
      i.account = INIT_ACCOUNT;
      from_string( i.url, INIT_URL );
      from_string( i.details, INIT_DETAILS );
      i.active = true;
      i.created = now;
      i.last_updated = now;
   });

   create< mediator_object >( [&]( mediator_object& i )
   {
      i.account = INIT_ACCOUNT;
      from_string( i.url, INIT_URL );
      from_string( i.details, INIT_DETAILS );
      i.active = true;
      i.created = now;
      i.last_updated = now;
   });

   // Create anonymous account for anonymous posting: password = "anonymouspassword"

   create< account_object >( [&]( account_object& a )
   {
      a.name = ANON_ACCOUNT;
      a.registrar = INIT_ACCOUNT;
      a.referrer = INIT_ACCOUNT;
      a.secure_public_key = get_public_key( a.name, "secure", ANON_ACCOUNT_PASSWORD );
      a.connection_public_key = get_public_key( a.name, "connection", ANON_ACCOUNT_PASSWORD );
      a.friend_public_key = get_public_key( a.name, "friend", ANON_ACCOUNT_PASSWORD );
      a.companion_public_key = get_public_key( a.name, "companion", ANON_ACCOUNT_PASSWORD );
      a.created = now;
      a.last_updated = now;
      a.last_vote_time = now;
      a.last_view_time = now;
      a.last_share_time = now;
      a.last_post = now;
      a.last_root_post = now;
      a.last_transfer_time = now;
      a.last_activity_reward = now;
      a.last_account_recovery = now;
      a.last_community_created = now;
      a.last_asset_created = now;
      from_string( a.json, "" );
      from_string( a.json_private, "" );
      from_string( a.details, INIT_DETAILS );
      from_string( a.url, INIT_URL );
      from_string( a.profile_image, INIT_IMAGE );
      a.membership = membership_tier_type::TOP_MEMBERSHIP;
      a.membership_expiration = time_point::maximum();
      a.mined = true;
      a.active = true;
      a.can_vote = true;
      a.revenue_share = false;
   });

   create< account_authority_object >( [&]( account_authority_object& auth )
   {
      auth.account = ANON_ACCOUNT;
      auth.owner_auth.add_authority( get_public_key( ANON_ACCOUNT, "owner", ANON_ACCOUNT_PASSWORD ), 1 );
      auth.owner_auth.weight_threshold = 1;
      auth.active_auth.add_authority( get_public_key( ANON_ACCOUNT, "active", ANON_ACCOUNT_PASSWORD ), 1 );
      auth.active_auth.weight_threshold = 1;
      auth.posting_auth.add_authority( get_public_key( ANON_ACCOUNT, "posting", ANON_ACCOUNT_PASSWORD ), 1 );
      auth.posting_auth.weight_threshold = 1;
   });

   create< account_permission_object >( [&]( account_permission_object& aao )
   {
      aao.account = ANON_ACCOUNT;
   });

   create< account_following_object >( [&]( account_following_object& afo )
   {
      afo.account = ANON_ACCOUNT;
      afo.last_updated = now;
   });

   create< account_object >( [&]( account_object& a )
   {
      a.name = PRODUCER_ACCOUNT;
      a.registrar = INIT_ACCOUNT;
      a.referrer = INIT_ACCOUNT;
      a.secure_public_key = get_public_key( a.name, "secure", INIT_ACCOUNT_PASSWORD );
      a.connection_public_key = get_public_key( a.name, "connection", INIT_ACCOUNT_PASSWORD );
      a.friend_public_key = get_public_key( a.name, "friend", INIT_ACCOUNT_PASSWORD );
      a.companion_public_key = get_public_key( a.name, "companion", INIT_ACCOUNT_PASSWORD );
      a.created = now;
      a.last_updated = now;
      a.last_vote_time = now;
      a.last_view_time = now;
      a.last_share_time = now;
      a.last_post = now;
      a.last_root_post = now;
      a.last_transfer_time = now;
      a.last_activity_reward = now;
      a.last_account_recovery = now;
      a.last_community_created = now;
      a.last_asset_created = now;
      from_string( a.json, "" );
      from_string( a.json_private, "" );
      from_string( a.details, INIT_DETAILS );
      from_string( a.url, INIT_URL );
      from_string( a.profile_image, INIT_IMAGE );
      a.membership = membership_tier_type::TOP_MEMBERSHIP;
      a.membership_expiration = time_point::maximum();
      a.mined = true;
      a.active = true;
      a.can_vote = true;
      a.revenue_share = false;
   });

   const account_authority_object& producer_auth = create< account_authority_object >( [&]( account_authority_object& auth )
   {
      auth.account = PRODUCER_ACCOUNT;
      auth.owner_auth.weight_threshold = 1;
      auth.active_auth.weight_threshold = 1;
      auth.posting_auth.weight_threshold = 1;
   });

   create< account_permission_object >( [&]( account_permission_object& aao )
   {
      aao.account = PRODUCER_ACCOUNT;
   });

   create< account_following_object >( [&]( account_following_object& afo )
   {
      afo.account = PRODUCER_ACCOUNT;
      afo.last_updated = now;
   });

   // Create NULL account, which cannot make operations. 

   create< account_object >( [&]( account_object& a )
   {
      a.name = NULL_ACCOUNT;
      a.registrar = INIT_ACCOUNT;
      a.referrer = INIT_ACCOUNT;
      a.secure_public_key = get_public_key( a.name, "secure", INIT_ACCOUNT_PASSWORD );
      a.connection_public_key = get_public_key( a.name, "connection", INIT_ACCOUNT_PASSWORD );
      a.friend_public_key = get_public_key( a.name, "friend", INIT_ACCOUNT_PASSWORD );
      a.companion_public_key = get_public_key( a.name, "companion", INIT_ACCOUNT_PASSWORD );
      a.created = now;
      a.last_updated = now;
      a.last_vote_time = now;
      a.last_view_time = now;
      a.last_share_time = now;
      a.last_post = now;
      a.last_root_post = now;
      a.last_transfer_time = now;
      a.last_activity_reward = now;
      a.last_account_recovery = now;
      a.last_community_created = now;
      a.last_asset_created = now;
      from_string( a.json, "" );
      from_string( a.json_private, "" );
      from_string( a.details, INIT_DETAILS );
      from_string( a.url, INIT_URL );
      from_string( a.profile_image, INIT_IMAGE );
      a.membership = membership_tier_type::TOP_MEMBERSHIP;
      a.membership_expiration = time_point::maximum();
      a.mined = true;
      a.active = true;
      a.can_vote = false;
      a.revenue_share = false;
   });

   create< account_authority_object >( [&]( account_authority_object& auth )
   {
      auth.account = NULL_ACCOUNT;
      auth.owner_auth.weight_threshold = 1;
      auth.active_auth.weight_threshold = 1;
      auth.posting_auth.weight_threshold = 1;
   });

   create< account_permission_object >( [&]( account_permission_object& aao )
   {
      aao.account = NULL_ACCOUNT;
   });

   create< account_following_object >( [&]( account_following_object& afo )
   {
      afo.account = NULL_ACCOUNT;
      afo.last_updated = now;
   });

   create< account_object >( [&]( account_object& a )
   {
      a.name = TEMP_ACCOUNT;
      a.registrar = INIT_ACCOUNT;
      a.referrer = INIT_ACCOUNT;
      a.secure_public_key = get_public_key( a.name, "secure", INIT_ACCOUNT_PASSWORD );
      a.connection_public_key = get_public_key( a.name, "connection", INIT_ACCOUNT_PASSWORD );
      a.friend_public_key = get_public_key( a.name, "friend", INIT_ACCOUNT_PASSWORD );
      a.companion_public_key = get_public_key( a.name, "companion", INIT_ACCOUNT_PASSWORD );
      a.created = now;
      a.last_updated = now;
      a.last_vote_time = now;
      a.last_view_time = now;
      a.last_share_time = now;
      a.last_post = now;
      a.last_root_post = now;
      a.last_transfer_time = now;
      a.last_activity_reward = now;
      a.last_account_recovery = now;
      a.last_community_created = now;
      a.last_asset_created = now;
      from_string( a.json, "" );
      from_string( a.json_private, "" );
      from_string( a.details, INIT_DETAILS );
      from_string( a.url, INIT_URL );
      from_string( a.profile_image, INIT_IMAGE );
      a.membership = membership_tier_type::TOP_MEMBERSHIP;
      a.membership_expiration = time_point::maximum();
      a.mined = true;
      a.active = true;
      a.can_vote = false;
      a.revenue_share = false;
   });

   create< account_authority_object >( [&]( account_authority_object& auth )
   {
      auth.account = TEMP_ACCOUNT;
      auth.owner_auth.weight_threshold = 0;
      auth.active_auth.weight_threshold = 0;
   });

   create< account_permission_object >( [&]( account_permission_object& aao )
   {
      aao.account = TEMP_ACCOUNT;
   });

   create< account_following_object >( [&]( account_following_object& afo )
   {
      afo.account = TEMP_ACCOUNT;
      afo.last_updated = now;
   });

   // Create COIN asset
   
   create< asset_object >( [&]( asset_object& a ) 
   {
      a.symbol = SYMBOL_COIN;
      a.max_supply = MAX_ASSET_SUPPLY;
      a.asset_type = asset_property_type::CURRENCY_ASSET;
      a.flags = 0;
      a.issuer_permissions = 0;
      a.issuer = NULL_ACCOUNT;
      a.unstake_intervals = 4;
      a.stake_intervals = 0;
      from_string( a.json, "" );
      from_string( a.details, COIN_DETAILS );
      from_string( a.url, INIT_URL );
      a.created = now;
      a.last_updated = now;
   });

   create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
   {
      a.symbol = SYMBOL_COIN;
   });

   create< asset_currency_data_object >( [&]( asset_currency_data_object& a )
   {
      a.symbol = SYMBOL_COIN;
      a.block_reward = BLOCK_REWARD;
      a.block_reward_reduction_percent = 0;
      a.block_reward_reduction_days = 0;
      a.content_reward_percent = CONTENT_REWARD_PERCENT;
      a.equity_asset = SYMBOL_EQUITY;
      a.equity_reward_percent = EQUITY_REWARD_PERCENT;
      a.producer_reward_percent = PRODUCER_REWARD_PERCENT;
      a.supernode_reward_percent = SUPERNODE_REWARD_PERCENT;
      a.power_reward_percent = POWER_REWARD_PERCENT;
      a.community_fund_reward_percent = COMMUNITY_FUND_REWARD_PERCENT;
      a.development_reward_percent = DEVELOPMENT_REWARD_PERCENT;
      a.marketing_reward_percent = MARKETING_REWARD_PERCENT;
      a.advocacy_reward_percent = ADVOCACY_REWARD_PERCENT;
      a.activity_reward_percent = ACTIVITY_REWARD_PERCENT;
      a.producer_block_reward_percent = PRODUCER_BLOCK_PERCENT;
      a.validation_reward_percent = PRODUCER_VALIDATOR_PERCENT;
      a.txn_stake_reward_percent = PRODUCER_TXN_STAKE_PERCENT;
      a.work_reward_percent = PRODUCER_WORK_PERCENT;
      a.producer_activity_reward_percent = PRODUCER_ACTIVITY_PERCENT;
   });

   // Create Equity asset

   create< asset_object >( [&]( asset_object& a )
   {
      a.symbol = SYMBOL_EQUITY;
      a.max_supply = INIT_EQUITY_SUPPLY;
      a.asset_type = asset_property_type::EQUITY_ASSET;
      a.flags = 0;
      a.issuer_permissions = 0;
      a.issuer = INIT_ACCOUNT;
      a.unstake_intervals = 0;
      a.stake_intervals = 4;
      from_string( a.json, "" );
      from_string( a.details, EQUITY_DETAILS );
      from_string( a.url, INIT_URL );
      a.created = now;
      a.last_updated = now;
   });

   create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
   {
      a.symbol = SYMBOL_EQUITY;
   });

   create< asset_equity_data_object >( [&]( asset_equity_data_object& a )
   {
      a.business_account = INIT_ACCOUNT;
      a.symbol = SYMBOL_EQUITY;
      a.dividend_share_percent = DIVIDEND_SHARE_PERCENT;
      a.liquid_dividend_percent = LIQUID_DIVIDEND_PERCENT;
      a.staked_dividend_percent = STAKED_DIVIDEND_PERCENT;
      a.savings_dividend_percent = SAVINGS_DIVIDEND_PERCENT;
      a.liquid_voting_rights = PERCENT_100;
      a.staked_voting_rights = PERCENT_100;
      a.savings_voting_rights = PERCENT_100;
      a.min_active_time = EQUITY_ACTIVITY_TIME;
      a.min_balance = BLOCKCHAIN_PRECISION;
      a.min_producers = EQUITY_MIN_PRODUCERS;
      a.boost_balance = EQUITY_BOOST_BALANCE;
      a.boost_activity = EQUITY_BOOST_ACTIVITY;
      a.boost_producers = EQUITY_BOOST_PRODUCERS;
      a.boost_top = EQUITY_BOOST_TOP_PERCENT;
   });

   // Create USD asset

   create< asset_object >( [&]( asset_object& a )
   {
      a.symbol = SYMBOL_USD;
      a.issuer = NULL_ACCOUNT;
      a.asset_type = asset_property_type::STABLECOIN_ASSET;
      a.max_supply = MAX_ASSET_SUPPLY;
      a.flags = int( asset_issuer_permission_flags::producer_fed_asset );
      a.issuer_permissions = 0;
      a.unstake_intervals = 4;
      a.stake_intervals = 0;
      from_string( a.json, "" );
      from_string( a.details, USD_DETAILS );
      from_string( a.url, INIT_URL );
      a.created = now;
      a.last_updated = now;
   });

   create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
   {
      a.symbol = SYMBOL_USD;
   });

   create< asset_stablecoin_data_object >( [&]( asset_stablecoin_data_object& a )
   {
      a.symbol = SYMBOL_USD;
      a.backing_asset = SYMBOL_COIN;
      a.current_feed_publication_time = now;
      a.feed_lifetime = PRICE_FEED_LIFETIME;
      a.minimum_feeds = 1;
      a.asset_settlement_delay = ASSET_SETTLEMENT_DELAY;
      a.asset_settlement_offset_percent = ASSET_SETTLEMENT_OFFSET;
      a.maximum_asset_settlement_volume = ASSET_SETTLEMENT_MAX_VOLUME;

      price_feed feed;
      feed.settlement_price = price( asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ), asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );

      a.feeds[ GENESIS_ACCOUNT_BASE_NAME ] = make_pair( now, feed );
      a.update_median_feeds( now );
   });

   // Create Credit asset
   
   create< asset_object >( [&]( asset_object& a )
   {
      a.symbol = SYMBOL_CREDIT;
      a.asset_type = asset_property_type::CREDIT_ASSET;
      a.flags = 0;
      a.issuer_permissions = 0;
      a.issuer = INIT_ACCOUNT;
      a.unstake_intervals = 4;
      a.stake_intervals = 0;
      from_string( a.json, "" );
      from_string( a.details, CREDIT_DETAILS );
      from_string( a.url, INIT_URL );
      a.created = now;
      a.last_updated = now;
   });

   create< asset_dynamic_data_object >([&](asset_dynamic_data_object& a) 
   {
      a.symbol = SYMBOL_CREDIT;
   });

   create< asset_credit_data_object >( [&]( asset_credit_data_object& a )
   {
      a.business_account = INIT_ACCOUNT;
      a.symbol = SYMBOL_CREDIT;
      a.buyback_asset = SYMBOL_USD;
      a.buyback_price = price( asset( 1, SYMBOL_USD ), asset( 1, SYMBOL_CREDIT ) );
      a.buyback_share_percent = BUYBACK_SHARE_PERCENT;
      a.liquid_fixed_interest_rate = LIQUID_FIXED_INTEREST_RATE;
      a.liquid_variable_interest_rate = LIQUID_VARIABLE_INTEREST_RATE;
      a.staked_fixed_interest_rate = STAKED_FIXED_INTEREST_RATE;
      a.staked_variable_interest_rate = STAKED_VARIABLE_INTEREST_RATE;
      a.savings_fixed_interest_rate = SAVINGS_FIXED_INTEREST_RATE;
      a.savings_variable_interest_rate = SAVINGS_VARIABLE_INTEREST_RATE;
      a.var_interest_range = VAR_INTEREST_RANGE;
   });

   const producer_schedule_object& pso = create< producer_schedule_object >( [&]( producer_schedule_object& pso )
   {
      pso.current_shuffled_producers.reserve( size_t( TOTAL_PRODUCERS ) );
      pso.num_scheduled_producers = TOTAL_PRODUCERS;
      pso.last_pow_update = now;
   });

   // Create accounts for genesis producers

   chain_properties chain_props;

   for( int i = 0; i < ( GENESIS_PRODUCER_AMOUNT + GENESIS_EXTRA_PRODUCERS ); ++i )
   {
      account_name_type producer_name = GENESIS_ACCOUNT_BASE_NAME + ( i ? fc::to_string( i ) : std::string() );

      create< account_object >( [&]( account_object& a )
      {
         a.name = producer_name;
         a.registrar = INIT_ACCOUNT;
         a.referrer = INIT_ACCOUNT;
         a.secure_public_key = get_public_key( a.name, "secure", INIT_ACCOUNT_PASSWORD );
         a.connection_public_key = get_public_key( a.name, "connection", INIT_ACCOUNT_PASSWORD );
         a.friend_public_key = get_public_key( a.name, "friend", INIT_ACCOUNT_PASSWORD );
         a.companion_public_key = get_public_key( a.name, "companion", INIT_ACCOUNT_PASSWORD );
         a.created = now;
         a.last_updated = now;
         a.last_vote_time = now;
         a.last_view_time = now;
         a.last_share_time = now;
         a.last_post = now;
         a.last_root_post = now;
         a.last_transfer_time = now;
         a.last_activity_reward = now;
         a.last_account_recovery = now;
         a.last_community_created = now;
         a.last_asset_created = now;
         from_string( a.json, "" );
         from_string( a.json_private, "" );
         from_string( a.details, INIT_DETAILS );
         from_string( a.url, INIT_URL );
         from_string( a.profile_image, INIT_IMAGE );
         a.membership = membership_tier_type::TOP_MEMBERSHIP;
         a.membership_expiration = time_point::maximum();
         a.mined = true;
         a.active = true;
         a.can_vote = true;
         a.revenue_share = false;
      });

      create< account_authority_object >( [&]( account_authority_object& auth )
      {
         auth.account = producer_name;
         auth.owner_auth.add_authority( get_public_key( auth.account, "owner", INIT_ACCOUNT_PASSWORD ), 1 );
         auth.owner_auth.weight_threshold = 1;
         auth.active_auth.add_authority( get_public_key( auth.account, "active", INIT_ACCOUNT_PASSWORD ), 1 );
         auth.active_auth.weight_threshold = 1;
         auth.posting_auth.add_authority( get_public_key( auth.account, "posting", INIT_ACCOUNT_PASSWORD ), 1 );
         auth.posting_auth.weight_threshold = 1;
      });

      create< account_permission_object >( [&]( account_permission_object& aao )
      {
         aao.account = producer_name;
      });

      create< account_following_object >( [&]( account_following_object& afo )
      {
         afo.account = producer_name;
         afo.last_updated = now;
      });

      create< producer_object >( [&]( producer_object& p )
      {
         p.owner = producer_name;
         p.props = chain_props;
         p.signing_key = get_public_key( p.owner, "producer", INIT_ACCOUNT_PASSWORD );
         p.schedule = producer_object::top_voting_producer;
         p.active = true;
         p.running_version = BLOCKCHAIN_VERSION;
         from_string( p.json, "" );
         from_string( p.details, INIT_DETAILS );
         from_string( p.url, INIT_URL );
         p.created = now;
         p.last_updated = now;
      });

      if( i < GENESIS_PRODUCER_AMOUNT )
      {
         modify( pso, [&]( producer_schedule_object& pso )
         {
            pso.current_shuffled_producers.push_back( producer_name );
         });

         modify( producer_auth, [&]( account_authority_object& a )
         {
            a.active_auth.add_authority( producer_name, 1 );  
         });
      }
   }

   create< community_object >( [&]( community_object& bo )
   {
      bo.name = INIT_COMMUNITY;
      bo.founder = INIT_ACCOUNT;
      bo.community_privacy = community_privacy_type::OPEN_PUBLIC_COMMUNITY;
      bo.community_public_key = get_public_key( INIT_COMMUNITY, "community", INIT_ACCOUNT_PASSWORD );
      bo.max_rating = 9;
      bo.url = INIT_URL;
      bo.details = INIT_DETAILS;
      bo.flags = 0;
      bo.permissions = COMMUNITY_PERMISSION_MASK;
      bo.created = now;
      bo.last_updated = now;
      bo.last_post = now;
      bo.last_root_post = now;
      bo.active = true;
   });

   create< community_member_object >( [&]( community_member_object& bmo )
   {
      bmo.name = INIT_COMMUNITY;
      bmo.founder = INIT_ACCOUNT;
      bmo.subscribers.insert( INIT_ACCOUNT );
      bmo.members.insert( INIT_ACCOUNT );
      bmo.moderators.insert( INIT_ACCOUNT );
      bmo.administrators.insert( INIT_ACCOUNT );
      bmo.community_privacy = community_privacy_type::OPEN_PUBLIC_COMMUNITY;
      bmo.last_updated = now;
   });

   create< community_moderator_vote_object >( [&]( community_moderator_vote_object& v )
   {
      v.moderator = INIT_ACCOUNT;
      v.account = INIT_ACCOUNT;
      v.community = INIT_COMMUNITY;
      v.vote_rank = 1;
   });

   // Allocate Genesis block reward to Init Account and create primary asset liquidity and credit pools.

   /**
    * Create Primary Liquidity Pools in Block 0.
    * 
    * [ coin/equity, coin/usd, coin/credit, equity/usd, equity/credit, usd/credit ]
    * Creates initial collateral positions of USD Asset, 
    * and rewards init account with small amount of Equity 
    * and Credit assets in liquidity and credit pools.
    */

   const asset_currency_data_object& currency = get_currency_data( SYMBOL_COIN );
   asset block_reward = currency.block_reward;
   FC_ASSERT( block_reward.symbol == SYMBOL_COIN && 
      block_reward.amount == 25 * BLOCKCHAIN_PRECISION, 
      "Block reward is not the correct symbol: ${s} or amount: ${a}",
      ("s",block_reward.symbol)("a",block_reward.amount));

   flat_set< asset_symbol_type > new_strikes;
   flat_set< asset_symbol_type > option_strikes;
   flat_set< date_type > new_dates;
   date_type next_date = date_type( now );

   for( int i = 0; i < 12; i++ )      // compile the next 12 months of expiration dates
   {
      if( next_date.month != 12 )
      {
         next_date = date_type( 1, next_date.month + 1, next_date.year );
      }
      else
      {
         next_date = date_type( 1, 1, next_date.year + 1 );
      }
      new_dates.insert( next_date );
   }

   adjust_liquid_balance( INIT_ACCOUNT, block_reward );
   adjust_liquid_balance( INIT_ACCOUNT, asset( 10 * BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ) );
   adjust_liquid_balance( INIT_ACCOUNT, asset( 10 * BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );

   asset liquid_coin = get_liquid_balance( INIT_ACCOUNT, SYMBOL_COIN );

   FC_ASSERT( liquid_coin.symbol == SYMBOL_COIN && 
      liquid_coin.amount == 25 * BLOCKCHAIN_PRECISION, 
      "INIT_ACCOUNT does not have correct balance - symbol: ${s} or amount: ${a}", 
      ("s",liquid_coin.symbol)("a",liquid_coin.amount) );

   create< call_order_object >( [&]( call_order_object& coo )
   {
      coo.borrower = INIT_ACCOUNT;
      coo.collateral = asset( 10 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      coo.debt = asset( 5 * BLOCKCHAIN_PRECISION, SYMBOL_USD );
      coo.created = now;
      coo.last_updated = now;
   });

   adjust_liquid_balance( INIT_ACCOUNT, -asset( 10 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
   adjust_pending_supply( asset( 10 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
   adjust_liquid_balance( INIT_ACCOUNT, asset( 5 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );

   asset_symbol_type coin_equity_symbol = string( LIQUIDITY_ASSET_PREFIX )+string( SYMBOL_COIN )+"."+string( SYMBOL_EQUITY );

   create< asset_object >( [&]( asset_object& a )
   {
      a.issuer = NULL_ACCOUNT;
      a.symbol = coin_equity_symbol;
      a.asset_type = asset_property_type::LIQUIDITY_POOL_ASSET;
   });

   create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
   {
      a.symbol = coin_equity_symbol;
   });

   create< asset_liquidity_pool_object >( [&]( asset_liquidity_pool_object& a )
   {   
      a.symbol_a = SYMBOL_COIN;
      a.symbol_b = SYMBOL_EQUITY;
      a.symbol_liquid = coin_equity_symbol;
      a.balance_a = asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      a.balance_b = asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY );
      a.balance_liquid = asset( BLOCKCHAIN_PRECISION, coin_equity_symbol );
      a.hour_median_price = price( a.balance_a, a.balance_b );
      a.day_median_price = price( a.balance_a, a.balance_b );
      a.price_history.push_back( price( a.balance_a, a.balance_b ) );
   });

   const asset_option_pool_object& coin_equity_option = create< asset_option_pool_object >( [&]( asset_option_pool_object& aopo )
   {   
      aopo.base_symbol = SYMBOL_COIN;
      aopo.quote_symbol = SYMBOL_EQUITY;
      aopo.add_strike_prices( asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) / asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ), new_dates );
   });

   new_strikes = coin_equity_option.get_strike_prices( asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) / asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ), new_dates );

   for( asset_symbol_type s : new_strikes )
   {
      option_strikes.insert( s );
   }
   
   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ) );
   adjust_liquid_balance( INIT_ACCOUNT, asset( BLOCKCHAIN_PRECISION, coin_equity_symbol ) );

   asset_symbol_type coin_usd_symbol = string( LIQUIDITY_ASSET_PREFIX )+string( SYMBOL_COIN )+"."+string( SYMBOL_USD );

   create< asset_object >( [&]( asset_object& a )
   {
      a.issuer = NULL_ACCOUNT;
      a.symbol = coin_usd_symbol;
      a.asset_type = asset_property_type::LIQUIDITY_POOL_ASSET;
   });

   create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
   {
      a.symbol = coin_usd_symbol;
   });

   create< asset_liquidity_pool_object >( [&]( asset_liquidity_pool_object& a )
   {   
      a.symbol_a = SYMBOL_COIN;
      a.symbol_b = SYMBOL_USD;
      a.symbol_liquid = coin_usd_symbol;
      a.balance_a = asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      a.balance_b = asset( BLOCKCHAIN_PRECISION, SYMBOL_USD );
      a.balance_liquid = asset( BLOCKCHAIN_PRECISION, coin_usd_symbol );
      a.hour_median_price = price( a.balance_a, a.balance_b );
      a.day_median_price = price( a.balance_a, a.balance_b );
      a.price_history.push_back( price( a.balance_a, a.balance_b ) );
   });

   const asset_option_pool_object& coin_usd_option = create< asset_option_pool_object >( [&]( asset_option_pool_object& aopo )
   {   
      aopo.base_symbol = SYMBOL_COIN;
      aopo.quote_symbol = SYMBOL_USD;
      aopo.add_strike_prices( asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) / asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ), new_dates );
   });

   new_strikes = coin_usd_option.get_strike_prices( asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) / asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ), new_dates );

   for( asset_symbol_type s : new_strikes )
   {
      option_strikes.insert( s );
   }

   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
   adjust_liquid_balance( INIT_ACCOUNT, asset( BLOCKCHAIN_PRECISION, coin_usd_symbol ) );

   asset_symbol_type coin_credit_symbol = string( LIQUIDITY_ASSET_PREFIX )+string( SYMBOL_COIN )+"."+string( SYMBOL_CREDIT );

   create< asset_object >( [&]( asset_object& a )
   {
      a.issuer = NULL_ACCOUNT;
      a.symbol = coin_credit_symbol;
      a.asset_type = asset_property_type::LIQUIDITY_POOL_ASSET;
   });

   create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
   {
      a.symbol = coin_credit_symbol;
   });

   create< asset_liquidity_pool_object >( [&]( asset_liquidity_pool_object& a )
   {   
      a.symbol_a = SYMBOL_COIN;
      a.symbol_b = SYMBOL_CREDIT;
      a.symbol_liquid = coin_credit_symbol;
      a.balance_a = asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      a.balance_b = asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT );
      a.balance_liquid = asset( BLOCKCHAIN_PRECISION, coin_credit_symbol );
      a.hour_median_price = price( a.balance_a, a.balance_b );
      a.day_median_price = price( a.balance_a, a.balance_b );
      a.price_history.push_back( price( a.balance_a, a.balance_b ) );
   });

   const asset_option_pool_object& coin_credit_option = create< asset_option_pool_object >( [&]( asset_option_pool_object& aopo )
   {   
      aopo.base_symbol = SYMBOL_COIN;
      aopo.quote_symbol = SYMBOL_CREDIT;
      aopo.add_strike_prices( asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) / asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ), new_dates );
   });

   new_strikes = coin_credit_option.get_strike_prices( asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) / asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ), new_dates );

   for( asset_symbol_type s : new_strikes )
   {
      option_strikes.insert( s );
   }

   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
   adjust_liquid_balance( INIT_ACCOUNT, asset( BLOCKCHAIN_PRECISION, coin_credit_symbol ) );

   asset_symbol_type equity_usd_symbol = string( LIQUIDITY_ASSET_PREFIX )+string( SYMBOL_EQUITY )+"."+string( SYMBOL_USD );

   create< asset_object >( [&]( asset_object& a )
   {
      a.issuer = NULL_ACCOUNT;
      a.symbol = equity_usd_symbol;
      a.asset_type = asset_property_type::LIQUIDITY_POOL_ASSET;
   });

   create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
   {
      a.symbol = equity_usd_symbol;
   });
      
   create< asset_liquidity_pool_object >( [&]( asset_liquidity_pool_object& a )
   {   
      a.symbol_a = SYMBOL_EQUITY;
      a.symbol_b = SYMBOL_USD;
      a.symbol_liquid = equity_usd_symbol;
      a.balance_a = asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY );
      a.balance_b = asset( BLOCKCHAIN_PRECISION, SYMBOL_USD );
      a.balance_liquid = asset( BLOCKCHAIN_PRECISION, equity_usd_symbol );
      a.hour_median_price = price( a.balance_a, a.balance_b );
      a.day_median_price = price( a.balance_a, a.balance_b );
      a.price_history.push_back( price( a.balance_a, a.balance_b ) );
   });

   const asset_option_pool_object& equity_usd_option = create< asset_option_pool_object >( [&]( asset_option_pool_object& aopo )
   {   
      aopo.base_symbol = SYMBOL_EQUITY;
      aopo.quote_symbol = SYMBOL_USD;
      aopo.add_strike_prices( asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ) / asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ), new_dates );
   });

   new_strikes = equity_usd_option.get_strike_prices( asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ) / asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ), new_dates );

   for( asset_symbol_type s : new_strikes )
   {
      option_strikes.insert( s );
   }

   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ) );
   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
   adjust_liquid_balance( INIT_ACCOUNT, asset( BLOCKCHAIN_PRECISION, equity_usd_symbol ) );

   asset_symbol_type equity_credit_symbol = string( LIQUIDITY_ASSET_PREFIX )+string( SYMBOL_EQUITY )+"."+string( SYMBOL_CREDIT );
   
   create< asset_object >( [&]( asset_object& a )
   {
      a.issuer = NULL_ACCOUNT;
      a.symbol = equity_credit_symbol;
      a.asset_type = asset_property_type::LIQUIDITY_POOL_ASSET;
   });

   create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
   {
      a.symbol = equity_credit_symbol;
   });
      
   create< asset_liquidity_pool_object >( [&]( asset_liquidity_pool_object& a )
   {   
      a.symbol_a = SYMBOL_EQUITY;
      a.symbol_b = SYMBOL_CREDIT;
      a.symbol_liquid = equity_credit_symbol;
      a.balance_a = asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY );
      a.balance_b = asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT );
      a.balance_liquid = asset( BLOCKCHAIN_PRECISION, equity_credit_symbol );
      a.hour_median_price = price( a.balance_a, a.balance_b );
      a.day_median_price = price( a.balance_a, a.balance_b );
      a.price_history.push_back( price( a.balance_a, a.balance_b ) );
   });

   const asset_option_pool_object& equity_credit_option = create< asset_option_pool_object >( [&]( asset_option_pool_object& aopo )
   {   
      aopo.base_symbol = SYMBOL_EQUITY;
      aopo.quote_symbol = SYMBOL_CREDIT;
      aopo.add_strike_prices( asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ) / asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ), new_dates );
   });

   new_strikes = equity_credit_option.get_strike_prices( asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ) / asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ), new_dates );

   for( asset_symbol_type s : new_strikes )
   {
      option_strikes.insert( s );
   }

   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ) );
   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
   adjust_liquid_balance( INIT_ACCOUNT, asset( BLOCKCHAIN_PRECISION, equity_credit_symbol ) );

   asset_symbol_type usd_credit_symbol = string( LIQUIDITY_ASSET_PREFIX )+string( SYMBOL_USD )+"."+string( SYMBOL_CREDIT );

   create< asset_object >( [&]( asset_object& a )
   {
      a.issuer = NULL_ACCOUNT;
      a.symbol = usd_credit_symbol;
      a.asset_type = asset_property_type::LIQUIDITY_POOL_ASSET;
   });

   create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
   {
      a.symbol = usd_credit_symbol;
   });

   create< asset_liquidity_pool_object >( [&]( asset_liquidity_pool_object& a )
   {   
      a.symbol_a = SYMBOL_USD;
      a.symbol_b = SYMBOL_CREDIT;
      a.symbol_liquid = usd_credit_symbol;
      a.balance_a = asset( BLOCKCHAIN_PRECISION, SYMBOL_USD );
      a.balance_b = asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT );
      a.balance_liquid = asset( BLOCKCHAIN_PRECISION, usd_credit_symbol );
      a.hour_median_price = price( a.balance_a, a.balance_b );
      a.day_median_price = price( a.balance_a, a.balance_b );
      a.price_history.push_back( price( a.balance_a, a.balance_b ) );
   });

   const asset_option_pool_object& usd_credit_option = create< asset_option_pool_object >( [&]( asset_option_pool_object& aopo )
   {   
      aopo.base_symbol = SYMBOL_USD;
      aopo.quote_symbol = SYMBOL_CREDIT;
      aopo.add_strike_prices( asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ) / asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ), new_dates );
   });

   new_strikes = usd_credit_option.get_strike_prices( asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ) / asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ), new_dates );

   for( asset_symbol_type s : new_strikes )
   {
      option_strikes.insert( s );
   }

   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
   adjust_liquid_balance( INIT_ACCOUNT, asset( BLOCKCHAIN_PRECISION, usd_credit_symbol ) );

   for( asset_symbol_type s : option_strikes )     // Create the new asset objects for the options.
   {
      option_strike strike = option_strike::from_string( s );
      const asset_object& base_asset = get_asset( strike.strike_price.base.symbol );
      const asset_object& quote_asset = get_asset( strike.strike_price.quote.symbol );

      create< asset_object >( [&]( asset_object& a )
      {
         a.symbol = s;
         a.asset_type = asset_property_type::OPTION_ASSET;
         a.issuer = NULL_ACCOUNT;
         from_string( a.display_symbol, strike.display_symbol() );
         from_string( 
            a.details, 
            strike.details( 
               to_string( quote_asset.display_symbol ), 
               to_string( quote_asset.details ), 
               to_string( base_asset.display_symbol ), 
               to_string( base_asset.details ) ) );
         
         from_string( a.json, "" );
         from_string( a.url, "" );
         a.max_supply = MAX_ASSET_SUPPLY;
         a.stake_intervals = 0;
         a.unstake_intervals = 0;
         a.market_fee_percent = 0;
         a.market_fee_share_percent = 0;
         a.issuer_permissions = 0;
         a.flags = 0;
         a.created = now;
         a.last_updated = now;
      });

      create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
      {
         a.symbol = s;
      });
   }

   // Create Primary asset credit pools [ coin, equity, usd, credit ]

   asset_symbol_type credit_asset_coin_symbol = string( CREDIT_ASSET_PREFIX )+string( SYMBOL_COIN );
   
   create< asset_object >( [&]( asset_object& a )
   {
      a.issuer = NULL_ACCOUNT;
      a.symbol = credit_asset_coin_symbol;
      a.asset_type = asset_property_type::CREDIT_POOL_ASSET;
   });

   create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
   {
      a.symbol = credit_asset_coin_symbol;
   });

   create< asset_credit_pool_object >( [&]( asset_credit_pool_object& a )
   {
      a.base_symbol = SYMBOL_COIN;
      a.credit_symbol = credit_asset_coin_symbol;
      a.base_balance = asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      a.borrowed_balance = asset( 0, SYMBOL_COIN );
      a.credit_balance = asset( 100 * BLOCKCHAIN_PRECISION, credit_asset_coin_symbol );
      a.last_price = price( a.base_balance, a.credit_balance );
   });

   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
   adjust_liquid_balance( INIT_ACCOUNT, asset( 100 * BLOCKCHAIN_PRECISION, credit_asset_coin_symbol ) );

   asset_symbol_type credit_asset_equity_symbol = string( CREDIT_ASSET_PREFIX )+string( SYMBOL_EQUITY );
   
   create< asset_object >( [&]( asset_object& a )
   {
      a.issuer = NULL_ACCOUNT;
      a.symbol = credit_asset_equity_symbol;
      a.asset_type = asset_property_type::CREDIT_POOL_ASSET; 
   });

   create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
   {
      a.symbol = credit_asset_equity_symbol;
   });

   create< asset_credit_pool_object >( [&]( asset_credit_pool_object& a )
   {
      a.base_symbol = SYMBOL_EQUITY;
      a.credit_symbol = credit_asset_equity_symbol;
      a.base_balance = asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY );
      a.borrowed_balance = asset( 0, SYMBOL_EQUITY );
      a.credit_balance = asset( 100 * BLOCKCHAIN_PRECISION, credit_asset_equity_symbol );
      a.last_price = price( a.base_balance, a.credit_balance );
   });

   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_EQUITY ) );
   adjust_liquid_balance( INIT_ACCOUNT, asset( 100 * BLOCKCHAIN_PRECISION, credit_asset_equity_symbol ) );

   asset_symbol_type credit_asset_usd_symbol = string( CREDIT_ASSET_PREFIX )+string( SYMBOL_USD );
   
   create< asset_object >( [&]( asset_object& a )
   {
      a.issuer = NULL_ACCOUNT;
      a.symbol = credit_asset_usd_symbol;
      a.asset_type = asset_property_type::CREDIT_POOL_ASSET; 
   });

   create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
   {
      a.symbol = credit_asset_usd_symbol;
   });

   create< asset_credit_pool_object >( [&]( asset_credit_pool_object& a )
   {
      a.base_symbol = SYMBOL_USD;
      a.credit_symbol = credit_asset_usd_symbol;
      a.base_balance = asset( BLOCKCHAIN_PRECISION, SYMBOL_USD );
      a.borrowed_balance = asset( 0, SYMBOL_USD );
      a.credit_balance = asset( 100 * BLOCKCHAIN_PRECISION, credit_asset_usd_symbol );
      a.last_price = price( a.base_balance, a.credit_balance );
   });

   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
   adjust_liquid_balance( INIT_ACCOUNT, asset( 100 * BLOCKCHAIN_PRECISION, credit_asset_usd_symbol ) );

   asset_symbol_type credit_asset_credit_symbol = string( CREDIT_ASSET_PREFIX )+string( SYMBOL_CREDIT );
   
   create< asset_object >( [&]( asset_object& a )
   {
      a.issuer = NULL_ACCOUNT;
      a.symbol = credit_asset_credit_symbol;
      a.asset_type = asset_property_type::CREDIT_POOL_ASSET;
   });

   create< asset_dynamic_data_object >( [&]( asset_dynamic_data_object& a )
   {
      a.symbol = credit_asset_credit_symbol;
   });

   create< asset_credit_pool_object >( [&]( asset_credit_pool_object& a )
   {
      a.base_symbol = SYMBOL_CREDIT;
      a.credit_symbol = credit_asset_credit_symbol;
      a.base_balance = asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT );
      a.borrowed_balance = asset( 0, SYMBOL_CREDIT );
      a.credit_balance = asset( 100 * BLOCKCHAIN_PRECISION, credit_asset_credit_symbol );
      a.last_price = price( a.base_balance, a.credit_balance );
   });

   adjust_liquid_balance( INIT_ACCOUNT, -asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
   adjust_pending_supply( asset( BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
   adjust_liquid_balance( INIT_ACCOUNT, asset( 100 * BLOCKCHAIN_PRECISION, credit_asset_credit_symbol ) );

} FC_CAPTURE_AND_RETHROW() }


void database::reindex( const fc::path& data_dir, const fc::path& shared_mem_dir, uint64_t shared_file_size )
{ try {
   ilog( "Reindexing Blockchain" );
   wipe( data_dir, shared_mem_dir, false );
   open( data_dir, shared_mem_dir, shared_file_size, chainbase::database::read_write );
   _fork_db.reset();    // override effect of _fork_db.start_block() call in open()

   auto start = fc::time_point::now();
   ASSERT( _block_log.head(), block_log_exception,
      "No blocks in block log. Cannot reindex an empty chain." );

   ilog( "Replaying blocks..." );

   uint64_t skip_flags =
      skip_producer_signature |
      skip_transaction_signatures |
      skip_transaction_dupe_check |
      skip_tapos_check |
      skip_merkle_check |
      skip_producer_schedule_check |
      skip_authority_check |
      skip_validate | /// no need to validate operations
      skip_validate_invariants |
      skip_block_log;

   with_write_lock( [&]()
   {
      auto itr = _block_log.read_block( 0 );
      auto last_block_num = _block_log.head()->block_num();

      while( itr.first.block_num() != last_block_num )
      {
         auto cur_block_num = itr.first.block_num();
         if( cur_block_num % 100000 == 0 )
            std::cerr << "   " << double( cur_block_num * 100 ) / last_block_num << "%   " << cur_block_num << " of " << last_block_num <<
            "   (" << (get_free_memory() / (1024*1024)) << "M free)\n";
         apply_block( itr.first, skip_flags );
         itr = _block_log.read_block( itr.second );
      }

      apply_block( itr.first, skip_flags );
      set_revision( head_block_num() );
   });

   if( _block_log.head()->block_num() )
      _fork_db.start_block( *_block_log.head() );

   auto end = fc::time_point::now();
   ilog( "Done reindexing, elapsed time: ${t} sec", ("t",double((end-start).count())/1000000.0 ) );
} FC_CAPTURE_AND_RETHROW( (data_dir)(shared_mem_dir) ) }


void database::wipe( const fc::path& data_dir, const fc::path& shared_mem_dir, bool include_blocks)
{
   close();
   chainbase::database::wipe( shared_mem_dir );
   if( include_blocks )
   {
      fc::remove_all( data_dir / "block_log" );
      fc::remove_all( data_dir / "block_log.index" );
   }
}


void database::close(bool rewind)
{
   try
   {
      // Since pop_block() will move tx's in the popped blocks into pending,
      // we have to clear_pending() after we're done popping to get a clean
      // DB state (issue #336).
      clear_pending();

      chainbase::database::flush();
      chainbase::database::close();

      _block_log.close();

      _fork_db.reset();
   }
   FC_CAPTURE_AND_RETHROW()
}


bool database::is_known_block( const block_id_type& id )const
{ try {
   return fetch_block_by_id( id ).valid();
} FC_CAPTURE_AND_RETHROW() }


/**
 * Only return true *if* the transaction has not expired or been invalidated. If this
 * method is called with a VERY old transaction we will return false, they should
 * query things by blocks if they are that old.
 */
bool database::is_known_transaction( const transaction_id_type& id )const
{ try {
   const auto& trx_idx = get_index<transaction_index>().indices().get<by_trx_id>();
   return trx_idx.find( id ) != trx_idx.end();
} FC_CAPTURE_AND_RETHROW() }


block_id_type database::find_block_id_for_num( uint64_t block_num )const
{ try {
   if( block_num == 0 )
      return block_id_type();

   // Reversible blocks are *usually* in the TAPOS buffer.  Since this
   // is the fastest check, we do it first.
   block_summary_id_type bsid = block_num & 0xFFFF;
   const block_summary_object* bs = find< block_summary_object, by_id >( bsid );
   if( bs != nullptr )
   {
      if( protocol::block_header::num_from_id(bs->block_id) == block_num )
         return bs->block_id;
   }

   // Next we query the block log.   Irreversible blocks are here.
   auto b = _block_log.read_block_by_num( block_num );
   if( b.valid() )
      return b->id();

   // Finally we query the fork DB.
   shared_ptr< fork_item > fitem = _fork_db.fetch_block_on_main_branch_by_number( block_num );
   if( fitem )
      return fitem->id;

   return block_id_type();
} FC_CAPTURE_AND_RETHROW( (block_num) ) }

block_id_type database::get_block_id_for_num( uint64_t block_num )const
{
   block_id_type bid = find_block_id_for_num( block_num );
   FC_ASSERT( bid != block_id_type() );
   return bid;
}

optional<signed_block> database::fetch_block_by_id( const block_id_type& id )const
{ try {
   auto b = _fork_db.fetch_block( id );
   if( !b )
   {
      auto tmp = _block_log.read_block_by_num( protocol::block_header::num_from_id( id ) );

      if( tmp && tmp->id() == id )
         return tmp;

      tmp.reset();
      return tmp;
   }

   return b->data;
} FC_CAPTURE_AND_RETHROW() }

optional<signed_block> database::fetch_block_by_number( uint64_t block_num )const
{ try {
   optional< signed_block > b;

   auto results = _fork_db.fetch_block_by_number( block_num );
   if( results.size() == 1 )
      b = results[0]->data;
   else
      b = _block_log.read_block_by_num( block_num );

   return b;
} FC_LOG_AND_RETHROW() }

const signed_transaction database::get_recent_transaction( const transaction_id_type& trx_id ) const
{ try {
   auto& index = get_index<transaction_index>().indices().get<by_trx_id>();
   auto itr = index.find(trx_id);
   FC_ASSERT(itr != index.end());
   signed_transaction trx;
   fc::raw::unpack( itr->packed_trx, trx );
   return trx;;
} FC_CAPTURE_AND_RETHROW() }


annotated_signed_transaction database::get_transaction( const transaction_id_type& id )const
{ try {

   #ifndef SKIP_BY_TX_ID

   const auto& txn_idx = get_index< operation_index >().indices().get< by_transaction_id >();
   auto txn_itr = txn_idx.lower_bound( id );

   if( txn_itr != txn_idx.end() && 
      txn_itr->trx_id == id )
   {
      auto blk = fetch_block_by_number( txn_itr->block );
      FC_ASSERT( blk.valid(), 
         "Block not found at block height." );
      FC_ASSERT( blk->transactions.size() > txn_itr->trx_in_block,
         "Transaction in Block index too high." );

      annotated_signed_transaction result = blk->transactions[txn_itr->trx_in_block];

      result.block_num = txn_itr->block;
      result.transaction_num = txn_itr->trx_in_block;

      return result;
   }

   #endif

   FC_ASSERT( false, "Unknown Transaction ${t}", ("t",id));

} FC_CAPTURE_AND_RETHROW( (id) ) }

std::vector< block_id_type > database::get_block_ids_on_fork( block_id_type head_of_fork ) const
{ try {
   pair<fork_database::branch_type, fork_database::branch_type> branches = _fork_db.fetch_branch_from(head_block_id(), head_of_fork);
   if( !((branches.first.back()->previous_id() == branches.second.back()->previous_id())) )
   {
      edump( (head_of_fork)
             (head_block_id())
             (branches.first.size())
             (branches.second.size()) );
      assert(branches.first.back()->previous_id() == branches.second.back()->previous_id());
   }
   std::vector< block_id_type > result;
   for( const item_ptr& fork_block : branches.second )
      result.emplace_back(fork_block->id);
   result.emplace_back(branches.first.back()->previous_id());
   return result;
} FC_CAPTURE_AND_RETHROW() }

chain_id_type database::get_chain_id() const
{
   return CHAIN_ID;
}

const dynamic_global_property_object& database::get_dynamic_global_properties()const
{ try {
   return get< dynamic_global_property_object, by_id >( 0 );
} FC_CAPTURE_AND_RETHROW() }

time_point database::head_block_time()const
{
   return get_dynamic_global_properties().time;
}

uint64_t database::head_block_num()const
{
   return get_dynamic_global_properties().head_block_number;
}

block_id_type database::head_block_id()const
{
   return get_dynamic_global_properties().head_block_id;
}

const producer_schedule_object& database::get_producer_schedule()const
{ try {
   return get< producer_schedule_object, by_id >( 0 );
} FC_CAPTURE_AND_RETHROW() }

const median_chain_property_object& database::get_median_chain_properties()const
{ try {
   return get< median_chain_property_object, by_id >( 0 );
} FC_CAPTURE_AND_RETHROW() }

x11 database::pow_difficulty()const
{
   return get_producer_schedule().pow_target_difficulty;
}

node_property_object& database::node_properties()
{
   return _node_property_object;
}

const node_property_object& database::get_node_properties() const
{
   return _node_property_object;
}

uint64_t database::last_non_undoable_block_num() const
{
   return get_dynamic_global_properties().last_irreversible_block_num;
}

const asset_object& database::get_core_asset() const
{
   return get< asset_object, by_id >( 0 );
}

const asset_object* database::find_core_asset() const
{
   return find< asset_object, by_id >( 0 );
}

const price& database::get_usd_price() const
{
   return get_stablecoin_data( SYMBOL_USD ).current_feed.settlement_price;
}

const asset_object& database::get_asset( const asset_symbol_type& symbol ) const
{ try {
   return get< asset_object, by_symbol >( symbol );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_object* database::find_asset( const asset_symbol_type& symbol ) const
{
   return find< asset_object, by_symbol >( symbol );
}

const asset_dynamic_data_object& database::get_core_dynamic_data() const
{
   return get< asset_dynamic_data_object, by_id >( 0 );
}

const asset_dynamic_data_object* database::find_core_dynamic_data() const
{
   return find< asset_dynamic_data_object, by_id >( 0 );
}

const asset_dynamic_data_object& database::get_dynamic_data( const asset_symbol_type& symbol ) const
{ try {
   return get< asset_dynamic_data_object, by_symbol >( (symbol) );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_dynamic_data_object* database::find_dynamic_data( const asset_symbol_type& symbol ) const
{
   return find< asset_dynamic_data_object, by_symbol >( (symbol) );
}

const asset_currency_data_object& database::get_currency_data( const asset_symbol_type& symbol ) const
{ try {
   return get< asset_currency_data_object, by_symbol >( (symbol) );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_currency_data_object* database::find_currency_data( const asset_symbol_type& symbol ) const
{
   return find< asset_currency_data_object, by_symbol >( (symbol) );
}

const asset_stablecoin_data_object& database::get_stablecoin_data( const asset_symbol_type& symbol ) const
{ try {
   return get< asset_stablecoin_data_object, by_symbol >( (symbol) );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_stablecoin_data_object* database::find_stablecoin_data( const asset_symbol_type& symbol ) const
{
   return find< asset_stablecoin_data_object, by_symbol >( (symbol) );
}

const asset_equity_data_object& database::get_equity_data( const asset_symbol_type& symbol ) const
{ try {
   return get< asset_equity_data_object, by_symbol >( (symbol) );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_equity_data_object* database::find_equity_data( const asset_symbol_type& symbol ) const
{
   return find< asset_equity_data_object, by_symbol >( (symbol) );
}

const asset_bond_data_object& database::get_bond_data( const asset_symbol_type& symbol ) const
{ try {
   return get< asset_bond_data_object, by_symbol >( (symbol) );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_bond_data_object* database::find_bond_data( const asset_symbol_type& symbol ) const
{
   return find< asset_bond_data_object, by_symbol >( (symbol) );
}

const asset_credit_data_object& database::get_credit_data( const asset_symbol_type& symbol ) const
{ try {
   return get< asset_credit_data_object, by_symbol >( (symbol) );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_credit_data_object* database::find_credit_data( const asset_symbol_type& symbol ) const
{
   return find< asset_credit_data_object, by_symbol >( (symbol) );
}

const asset_stimulus_data_object& database::get_stimulus_data( const asset_symbol_type& symbol ) const
{ try {
   return get< asset_stimulus_data_object, by_symbol >( (symbol) );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_stimulus_data_object* database::find_stimulus_data( const asset_symbol_type& symbol ) const
{
   return find< asset_stimulus_data_object, by_symbol >( (symbol) );
}

const asset_unique_data_object& database::get_unique_data( const asset_symbol_type& symbol ) const
{ try {
   return get< asset_unique_data_object, by_symbol >( (symbol) );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_unique_data_object* database::find_unique_data( const asset_symbol_type& symbol ) const
{
   return find< asset_unique_data_object, by_symbol >( (symbol) );
}

const account_object& database::get_account( const account_name_type& name )const
{ try {
	return get< account_object, by_name >( name );
} FC_CAPTURE_AND_RETHROW( (name) ) }

const account_object* database::find_account( const account_name_type& name )const
{
   return find< account_object, by_name >( name );
}

const account_verification_object& database::get_account_verification( const account_name_type& verifier_account, const account_name_type& verified_account )const
{ try {
	return get< account_verification_object, by_verifier_verified >( boost::make_tuple( verifier_account, verified_account ) );
} FC_CAPTURE_AND_RETHROW( (verifier_account)(verified_account) ) }

const account_verification_object* database::find_account_verification( const account_name_type& verifier_account, const account_name_type& verified_account )const
{
   return find< account_verification_object, by_verifier_verified >( boost::make_tuple( verifier_account, verified_account ) );
}

const account_following_object& database::get_account_following( const account_name_type& account )const
{ try {
	return get< account_following_object, by_account >( account );
} FC_CAPTURE_AND_RETHROW( (account) ) }

const account_following_object* database::find_account_following( const account_name_type& account )const
{
   return find< account_following_object, by_account >( account );
}

const tag_following_object& database::get_tag_following( const tag_name_type& tag )const
{ try {
	return get< tag_following_object, by_tag >( tag );
} FC_CAPTURE_AND_RETHROW( (tag) ) }

const tag_following_object* database::find_tag_following( const tag_name_type& tag )const
{
   return find< tag_following_object, by_tag >( tag );
}

const account_business_object& database::get_account_business( const account_name_type& account )const
{ try {
	return get< account_business_object, by_account >( account );
} FC_CAPTURE_AND_RETHROW( (account) ) }

const account_business_object* database::find_account_business( const account_name_type& account )const
{
   return find< account_business_object, by_account >( account );
}

const account_executive_vote_object& database::get_account_executive_vote( const account_name_type& account, const account_name_type& business, const account_name_type& executive )const
{ try {
	return get< account_executive_vote_object, by_account_business_executive >( boost::make_tuple( account, business, executive ) );
} FC_CAPTURE_AND_RETHROW( (account)(business)(executive) ) }

const account_executive_vote_object* database::find_account_executive_vote( const account_name_type& account, const account_name_type& business, const account_name_type& executive )const
{
   return find< account_executive_vote_object, by_account_business_executive >( boost::make_tuple( account, business, executive ) );
}

const account_officer_vote_object& database::get_account_officer_vote( const account_name_type& account, const account_name_type& business, const account_name_type& officer )const
{ try {
	return get< account_officer_vote_object, by_account_business_officer >( boost::make_tuple( account, business, officer ) );
} FC_CAPTURE_AND_RETHROW( (account)(business)(officer) ) }

const account_officer_vote_object* database::find_account_officer_vote( const account_name_type& account, const account_name_type& business, const account_name_type& officer )const
{
   return find< account_officer_vote_object, by_account_business_officer >( boost::make_tuple( account, business, officer ) );
}

const account_member_request_object& database::get_account_member_request( const account_name_type& account, const account_name_type& business )const
{ try {
	return get< account_member_request_object, by_account_business >( boost::make_tuple( account, business) );
} FC_CAPTURE_AND_RETHROW( (account)(business) ) }

const account_member_request_object* database::find_account_member_request( const account_name_type& account, const account_name_type& business )const
{
   return find< account_member_request_object, by_account_business >( boost::make_tuple( account, business) );
}

const account_member_invite_object& database::get_account_member_invite( const account_name_type& member, const account_name_type& business )const
{ try {
	return get< account_member_invite_object, by_member_business >( boost::make_tuple( member, business) );
} FC_CAPTURE_AND_RETHROW( (member)(business) ) }

const account_member_invite_object* database::find_account_member_invite( const account_name_type& member, const account_name_type& business )const
{
   return find< account_member_invite_object, by_member_business >( boost::make_tuple( member, business) );
}

const account_member_key_object& database::get_account_member_key( const account_name_type& member, const account_name_type& business )const
{ try {
	return get< account_member_key_object, by_member_business >( boost::make_tuple( member, business) );
} FC_CAPTURE_AND_RETHROW( (member)(business) ) }

const account_member_key_object* database::find_account_member_key( const account_name_type& member, const account_name_type& business )const
{
   return find< account_member_key_object, by_member_business >( boost::make_tuple( member, business) );
}

const account_balance_object& database::get_account_balance( const account_name_type& owner, const asset_symbol_type& symbol )const
{ try {
	return get< account_balance_object, by_owner_symbol >( boost::make_tuple( owner, symbol ) );
} FC_CAPTURE_AND_RETHROW( (owner) (symbol) ) }

const account_balance_object* database::find_account_balance( const account_name_type& owner, const asset_symbol_type& symbol )const
{
   return find< account_balance_object, by_owner_symbol >( boost::make_tuple( owner, symbol ) );
}

const confidential_balance_object& database::get_confidential_balance( const digest_type& hash )const
{ try {
	return get< confidential_balance_object, by_hash >( hash );
} FC_CAPTURE_AND_RETHROW( (hash) ) }

const confidential_balance_object* database::find_confidential_balance( const digest_type& hash )const
{
   return find< confidential_balance_object, by_hash >( hash );
}

const asset_delegation_object& database::get_asset_delegation( const account_name_type& delegator, const account_name_type& delegatee, const asset_symbol_type& symbol )const
{ try {
	return get< asset_delegation_object, by_delegator >( boost::make_tuple( delegator, delegatee, symbol ) );
} FC_CAPTURE_AND_RETHROW( (delegator)(delegatee)(symbol) ) }

const asset_delegation_object* database::find_asset_delegation( const account_name_type& delegator, const account_name_type& delegatee, const asset_symbol_type& symbol )const
{
   return find< asset_delegation_object, by_delegator >( boost::make_tuple( delegator, delegatee, symbol ) );
}

const account_permission_object& database::get_account_permissions( const account_name_type& account )const
{ try {
	return get< account_permission_object, by_account >( account );
} FC_CAPTURE_AND_RETHROW( (account) ) }

const account_permission_object* database::find_account_permissions( const account_name_type& account )const
{
   return find< account_permission_object, by_account >( account );
}

const account_authority_object& database::get_account_authority( const account_name_type& account )const
{ try {
	return get< account_authority_object, by_account >( account );
} FC_CAPTURE_AND_RETHROW( (account) ) }

const account_authority_object* database::find_account_authority( const account_name_type& account )const
{
   return find< account_authority_object, by_account >( account );
}

const producer_object& database::get_producer( const account_name_type& name ) const
{ try {
   return get< producer_object, by_name >( name );
} FC_CAPTURE_AND_RETHROW( (name) ) }

const producer_object* database::find_producer( const account_name_type& name ) const
{
   return find< producer_object, by_name >( name );
}

const producer_vote_object& database::get_producer_vote( const account_name_type& account, const account_name_type& producer )const
{ try {
   return get< producer_vote_object, by_account_producer >( boost::make_tuple( account, producer ) );
} FC_CAPTURE_AND_RETHROW( (account)(producer) ) }

const producer_vote_object* database::find_producer_vote( const account_name_type& account, const account_name_type& producer )const
{
   return find< producer_vote_object, by_account_producer >( boost::make_tuple( account, producer ) );
}

const block_validation_object& database::get_block_validation( const account_name_type& producer, uint64_t height ) const
{ try {
   return get< block_validation_object, by_producer_height >( boost::make_tuple( producer, height) );
} FC_CAPTURE_AND_RETHROW( (producer)(height) ) }

const block_validation_object* database::find_block_validation( const account_name_type& producer, uint64_t height ) const
{
   return find< block_validation_object, by_producer_height >( boost::make_tuple( producer, height) );
}

const network_officer_object& database::get_network_officer( const account_name_type& account )const
{ try {
	return get< network_officer_object, by_account >( account );
} FC_CAPTURE_AND_RETHROW( (account) ) }

const network_officer_object* database::find_network_officer( const account_name_type& account )const
{
   return find< network_officer_object, by_account >( account );
}

const network_officer_vote_object& database::get_network_officer_vote( const account_name_type& account, const account_name_type& officer )const
{ try {
   return get< network_officer_vote_object, by_account_officer >( boost::make_tuple( account, officer ) );
} FC_CAPTURE_AND_RETHROW( (account)(officer) ) }

const network_officer_vote_object* database::find_network_officer_vote( const account_name_type& account, const account_name_type& officer )const
{
   return find< network_officer_vote_object, by_account_officer >( boost::make_tuple( account, officer ) );
}

const executive_board_object& database::get_executive_board( const account_name_type& account )const
{ try {
	return get< executive_board_object, by_account >( account );
} FC_CAPTURE_AND_RETHROW( (account) ) }

const executive_board_object* database::find_executive_board( const account_name_type& account )const
{
   return find< executive_board_object, by_account >( account );
}

const executive_board_vote_object& database::get_executive_board_vote( const account_name_type& account, const account_name_type& executive )const
{ try {
   return get< executive_board_vote_object, by_account_executive >( boost::make_tuple( account, executive ) );
} FC_CAPTURE_AND_RETHROW( (account)(executive) ) }

const executive_board_vote_object* database::find_executive_board_vote( const account_name_type& account, const account_name_type& executive )const
{
   return find< executive_board_vote_object, by_account_executive >( boost::make_tuple( account, executive ) );
}

const supernode_object& database::get_supernode( const account_name_type& account )const
{ try {
	return get< supernode_object, by_account >( account );
} FC_CAPTURE_AND_RETHROW( (account) ) }

const supernode_object* database::find_supernode( const account_name_type& account )const
{
   return find< supernode_object, by_account >( account );
}

const interface_object& database::get_interface( const account_name_type& account )const
{ try {
	return get< interface_object, by_account >( account );
} FC_CAPTURE_AND_RETHROW( (account) ) }

const interface_object* database::find_interface( const account_name_type& account )const
{
   return find< interface_object, by_account >( account );
}

const mediator_object& database::get_mediator( const account_name_type& account )const
{ try {
	return get< mediator_object, by_account >( account );
} FC_CAPTURE_AND_RETHROW( (account) ) }

const mediator_object* database::find_mediator( const account_name_type& account )const
{
   return find< mediator_object, by_account >( account );
}

const governance_account_object& database::get_governance_account( const account_name_type& account )const
{ try {
	return get< governance_account_object, by_account >( account );
} FC_CAPTURE_AND_RETHROW( (account) ) }

const governance_account_object* database::find_governance_account( const account_name_type& account )const
{
   return find< governance_account_object, by_account >( account );
}

const community_enterprise_object& database::get_community_enterprise( const account_name_type& creator, const shared_string& enterprise_id )const
{ try {
   return get< community_enterprise_object, by_enterprise_id >( boost::make_tuple( creator, enterprise_id ) );
} FC_CAPTURE_AND_RETHROW( (creator)(enterprise_id) ) }

const community_enterprise_object* database::find_community_enterprise( const account_name_type& creator, const shared_string& enterprise_id )const
{
   return find< community_enterprise_object, by_enterprise_id >( boost::make_tuple( creator, enterprise_id ) );
}

const community_enterprise_object& database::get_community_enterprise( const account_name_type& creator, const string& enterprise_id )const
{ try {
   return get< community_enterprise_object, by_enterprise_id >( boost::make_tuple( creator, enterprise_id ) );
} FC_CAPTURE_AND_RETHROW( (creator)(enterprise_id) ) }

const community_enterprise_object* database::find_community_enterprise( const account_name_type& creator, const string& enterprise_id )const
{
   return find< community_enterprise_object, by_enterprise_id >( boost::make_tuple( creator, enterprise_id ) );
}

const enterprise_approval_object& database::get_enterprise_approval( const account_name_type& creator, const shared_string& enterprise_id, const account_name_type& account )const
{ try {
   return get< enterprise_approval_object, by_enterprise_id >( boost::make_tuple( creator, enterprise_id, account ) );
} FC_CAPTURE_AND_RETHROW( (creator)(enterprise_id)(account) ) }

const enterprise_approval_object* database::find_enterprise_approval( const account_name_type& creator, const shared_string& enterprise_id, const account_name_type& account )const
{
   return find< enterprise_approval_object, by_enterprise_id >( boost::make_tuple( creator, enterprise_id, account ) );
}

const enterprise_approval_object& database::get_enterprise_approval( const account_name_type& creator, const string& enterprise_id, const account_name_type& account )const
{ try {
   return get< enterprise_approval_object, by_enterprise_id >( boost::make_tuple( creator, enterprise_id, account ) );
} FC_CAPTURE_AND_RETHROW( (creator)(enterprise_id)(account) ) }

const enterprise_approval_object* database::find_enterprise_approval( const account_name_type& creator, const string& enterprise_id, const account_name_type& account )const
{
   return find< enterprise_approval_object, by_enterprise_id >( boost::make_tuple( creator, enterprise_id, account ) );
}

const community_object& database::get_community( const community_name_type& community )const
{ try {
	return get< community_object, by_name >( community );
} FC_CAPTURE_AND_RETHROW( (community) ) }

const community_object* database::find_community( const community_name_type& community )const
{
   return find< community_object, by_name >( community );
}

const community_member_object& database::get_community_member( const community_name_type& community )const
{ try {
	return get< community_member_object, by_name >( community );
} FC_CAPTURE_AND_RETHROW( (community) ) }

const community_member_object* database::find_community_member( const community_name_type& community )const
{
   return find< community_member_object, by_name >( community );
}

const community_member_key_object& database::get_community_member_key( const account_name_type& member, const community_name_type& community )const
{ try {
	return get< community_member_key_object, by_member_community >( boost::make_tuple( member, community) );
} FC_CAPTURE_AND_RETHROW( (community) ) }

const community_member_key_object* database::find_community_member_key( const account_name_type& member, const community_name_type& community )const
{
   return find< community_member_key_object, by_member_community >( boost::make_tuple( member, community) );
}

const community_event_object& database::get_community_event( const community_name_type& community )const
{ try {
	return get< community_event_object, by_community >( community );
} FC_CAPTURE_AND_RETHROW( (community) ) }

const community_event_object* database::find_community_event( const community_name_type& community )const
{
   return find< community_event_object, by_community >( community );
}

const comment_object& database::get_comment( const account_name_type& author, const shared_string& permlink )const
{ try {
   return get< comment_object, by_permlink >( boost::make_tuple( author, permlink ) );
} FC_CAPTURE_AND_RETHROW( (author)(permlink) ) }

const comment_object* database::find_comment( const account_name_type& author, const shared_string& permlink )const
{
   return find< comment_object, by_permlink >( boost::make_tuple( author, permlink ) );
}

const comment_object& database::get_comment( const account_name_type& author, const string& permlink )const
{ try {
   return get< comment_object, by_permlink >( boost::make_tuple( author, permlink) );
} FC_CAPTURE_AND_RETHROW( (author)(permlink) ) }

const comment_object* database::find_comment( const account_name_type& author, const string& permlink )const
{
   return find< comment_object, by_permlink >( boost::make_tuple( author, permlink ) );
}

const comment_vote_object& database::get_comment_vote( const account_name_type& voter, const comment_id_type& vote_id )const
{ try {
   return get< comment_vote_object, by_voter_comment >( boost::make_tuple( voter, vote_id ) );
} FC_CAPTURE_AND_RETHROW( (voter)(vote_id) ) }

const comment_vote_object* database::find_comment_vote( const account_name_type& voter, const comment_id_type& vote_id )const
{
   return find< comment_vote_object, by_voter_comment >( boost::make_tuple( voter, vote_id ) );
}

const comment_view_object& database::get_comment_view( const account_name_type& viewer, const comment_id_type& view_id )const
{ try {
   return get< comment_view_object, by_viewer_comment >( boost::make_tuple( viewer, view_id ) );
} FC_CAPTURE_AND_RETHROW( (viewer)(view_id) ) }

const comment_view_object* database::find_comment_view( const account_name_type& viewer, const comment_id_type& view_id )const
{
   return find< comment_view_object, by_viewer_comment >( boost::make_tuple( viewer, view_id ) );
}

const comment_share_object& database::get_comment_share( const account_name_type& sharer, const comment_id_type& share_id )const
{ try {
   return get< comment_share_object, by_sharer_comment >( boost::make_tuple( sharer, share_id ) );
} FC_CAPTURE_AND_RETHROW( (sharer)(share_id) ) }

const comment_share_object* database::find_comment_share( const account_name_type& sharer, const comment_id_type& share_id )const
{
   return find< comment_share_object, by_sharer_comment >( boost::make_tuple( sharer, share_id ) );
}

const list_object& database::get_list( const account_name_type& creator, const shared_string& list_id )const
{ try {
   return get< list_object, by_list_id >( boost::make_tuple( creator, list_id ) );
} FC_CAPTURE_AND_RETHROW( (creator)(list_id) ) }

const list_object* database::find_list( const account_name_type& creator, const shared_string& list_id )const
{
   return find< list_object, by_list_id >( boost::make_tuple( creator, list_id ) );
}

const list_object& database::get_list( const account_name_type& creator, const string& list_id )const
{ try {
   return get< list_object, by_list_id >( boost::make_tuple( creator, list_id ) );
} FC_CAPTURE_AND_RETHROW( (creator)(list_id) ) }

const list_object* database::find_list( const account_name_type& creator, const string& list_id )const
{
   return find< list_object, by_list_id >( boost::make_tuple( creator, list_id ) );
}

const poll_object& database::get_poll( const account_name_type& creator, const shared_string& poll_id )const
{ try {
   return get< poll_object, by_poll_id >( boost::make_tuple( creator, poll_id ) );
} FC_CAPTURE_AND_RETHROW( (creator)(poll_id) ) }

const poll_object* database::find_poll( const account_name_type& creator, const shared_string& poll_id )const
{
   return find< poll_object, by_poll_id >( boost::make_tuple( creator, poll_id ) );
}

const poll_object& database::get_poll( const account_name_type& creator, const string& poll_id )const
{ try {
   return get< poll_object, by_poll_id >( boost::make_tuple( creator, poll_id ) );
} FC_CAPTURE_AND_RETHROW( (creator)(poll_id) ) }

const poll_object* database::find_poll( const account_name_type& creator, const string& poll_id )const
{
   return find< poll_object, by_poll_id >( boost::make_tuple( creator, poll_id ) );
}

const poll_vote_object& database::get_poll_vote( const account_name_type& voter, const account_name_type& creator, const shared_string& poll_id )const
{ try {
   return get< poll_vote_object, by_voter_creator_poll_id >( boost::make_tuple( voter, creator, poll_id ) );
} FC_CAPTURE_AND_RETHROW( (voter)(creator)(poll_id) ) }

const poll_vote_object* database::find_poll_vote( const account_name_type& voter, const account_name_type& creator, const shared_string& poll_id )const
{
   return find< poll_vote_object, by_voter_creator_poll_id >( boost::make_tuple( voter, creator, poll_id ) );
}

const poll_vote_object& database::get_poll_vote( const account_name_type& voter, const account_name_type& creator, const string& poll_id )const
{ try {
   return get< poll_vote_object, by_voter_creator_poll_id >( boost::make_tuple( voter, creator, poll_id ) );
} FC_CAPTURE_AND_RETHROW( (voter)(creator)(poll_id) ) }

const poll_vote_object* database::find_poll_vote( const account_name_type& voter, const account_name_type& creator, const string& poll_id )const
{
   return find< poll_vote_object, by_voter_creator_poll_id >( boost::make_tuple( voter, creator, poll_id ) );
}

const ad_creative_object& database::get_ad_creative( const account_name_type& account, const shared_string& creative_id )const
{ try {
   return get< ad_creative_object, by_creative_id >( boost::make_tuple( account, creative_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(creative_id) ) }

const ad_creative_object* database::find_ad_creative( const account_name_type& account, const shared_string& creative_id )const
{
   return find< ad_creative_object, by_creative_id >( boost::make_tuple( account, creative_id ) );
}

const ad_creative_object& database::get_ad_creative( const account_name_type& account, const string& creative_id )const
{ try {
   return get< ad_creative_object, by_creative_id >( boost::make_tuple( account, creative_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(creative_id) ) }

const ad_creative_object* database::find_ad_creative( const account_name_type& account, const string& creative_id )const
{
   return find< ad_creative_object, by_creative_id >( boost::make_tuple( account, creative_id ) );
}

const ad_campaign_object& database::get_ad_campaign( const account_name_type& account, const shared_string& campaign_id )const
{ try {
   return get< ad_campaign_object, by_campaign_id >( boost::make_tuple( account, campaign_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(campaign_id) ) }

const ad_campaign_object* database::find_ad_campaign( const account_name_type& account, const shared_string& campaign_id )const
{
   return find< ad_campaign_object, by_campaign_id >( boost::make_tuple( account, campaign_id ) );
}

const ad_campaign_object& database::get_ad_campaign( const account_name_type& account, const string& campaign_id )const
{ try {
   return get< ad_campaign_object, by_campaign_id >( boost::make_tuple( account, campaign_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(campaign_id) ) }

const ad_campaign_object* database::find_ad_campaign( const account_name_type& account, const string& campaign_id )const
{
   return find< ad_campaign_object, by_campaign_id >( boost::make_tuple( account, campaign_id ) );
}

const ad_inventory_object& database::get_ad_inventory( const account_name_type& account, const shared_string& inventory_id )const
{ try {
   return get< ad_inventory_object, by_inventory_id >( boost::make_tuple( account, inventory_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(inventory_id) ) }

const ad_inventory_object* database::find_ad_inventory( const account_name_type& account, const shared_string& inventory_id )const
{
   return find< ad_inventory_object, by_inventory_id >( boost::make_tuple( account, inventory_id ) );
}

const ad_inventory_object& database::get_ad_inventory( const account_name_type& account, const string& inventory_id )const
{ try {
   return get< ad_inventory_object, by_inventory_id >( boost::make_tuple( account, inventory_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(inventory_id) ) }

const ad_inventory_object* database::find_ad_inventory( const account_name_type& account, const string& inventory_id )const
{
   return find< ad_inventory_object, by_inventory_id >( boost::make_tuple( account, inventory_id ) );
}

const ad_audience_object& database::get_ad_audience( const account_name_type& account, const shared_string& audience_id )const
{ try {
   return get< ad_audience_object, by_audience_id >( boost::make_tuple( account, audience_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(audience_id) ) }

const ad_audience_object* database::find_ad_audience( const account_name_type& account, const shared_string& audience_id )const
{
   return find< ad_audience_object, by_audience_id >( boost::make_tuple( account, audience_id ) );
}

const ad_audience_object& database::get_ad_audience( const account_name_type& account, const string& audience_id )const
{ try {
   return get< ad_audience_object, by_audience_id >( boost::make_tuple( account, audience_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(audience_id) ) }

const ad_audience_object* database::find_ad_audience( const account_name_type& account, const string& audience_id )const
{
   return find< ad_audience_object, by_audience_id >( boost::make_tuple( account, audience_id ) );
}

const ad_bid_object& database::get_ad_bid( const account_name_type& account, const shared_string& bid_id )const
{ try {
   return get< ad_bid_object, by_bid_id >( boost::make_tuple( account, bid_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(bid_id) ) }

const ad_bid_object* database::find_ad_bid( const account_name_type& account, const shared_string& bid_id )const
{
   return find< ad_bid_object, by_bid_id >( boost::make_tuple( account, bid_id ) );
}

const ad_bid_object& database::get_ad_bid( const account_name_type& account, const string& bid_id )const
{ try {
   return get< ad_bid_object, by_bid_id >( boost::make_tuple( account, bid_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(bid_id) ) }

const ad_bid_object* database::find_ad_bid( const account_name_type& account, const string& bid_id )const
{
   return find< ad_bid_object, by_bid_id >( boost::make_tuple( account, bid_id ) );
}

const graph_node_object& database::get_graph_node( const account_name_type& account, const shared_string& node_id )const
{ try {
   return get< graph_node_object, by_account_id >( boost::make_tuple( account, node_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(node_id) ) }

const graph_node_object* database::find_graph_node( const account_name_type& account, const shared_string& node_id )const
{
   return find< graph_node_object, by_account_id >( boost::make_tuple( account, node_id ) );
}

const graph_node_object& database::get_graph_node( const account_name_type& account, const string& node_id )const
{ try {
   return get< graph_node_object, by_account_id >( boost::make_tuple( account, node_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(node_id) ) }

const graph_node_object* database::find_graph_node( const account_name_type& account, const string& node_id )const
{
   return find< graph_node_object, by_account_id >( boost::make_tuple( account, node_id ) );
}

const graph_edge_object& database::get_graph_edge( const account_name_type& account, const shared_string& edge_id )const
{ try {
   return get< graph_edge_object, by_account_id >( boost::make_tuple( account, edge_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(edge_id) ) }

const graph_edge_object* database::find_graph_edge( const account_name_type& account, const shared_string& edge_id )const
{
   return find< graph_edge_object, by_account_id >( boost::make_tuple( account, edge_id ) );
}

const graph_edge_object& database::get_graph_edge( const account_name_type& account, const string& edge_id )const
{ try {
   return get< graph_edge_object, by_account_id >( boost::make_tuple( account, edge_id ) );
} FC_CAPTURE_AND_RETHROW( (account)(edge_id) ) }

const graph_edge_object* database::find_graph_edge( const account_name_type& account, const string& edge_id )const
{
   return find< graph_edge_object, by_account_id >( boost::make_tuple( account, edge_id ) );
}

const graph_node_property_object& database::get_graph_node_property( const graph_node_name_type& node_type )const
{ try {
   return get< graph_node_property_object, by_node_type >( node_type );
} FC_CAPTURE_AND_RETHROW( (node_type) ) }

const graph_node_property_object* database::find_graph_node_property( const graph_node_name_type& node_type )const
{
   return find< graph_node_property_object, by_node_type >( node_type );
}

const graph_edge_property_object& database::get_graph_edge_property( const graph_edge_name_type& edge_type )const
{ try {
   return get< graph_edge_property_object, by_edge_type >( edge_type );
} FC_CAPTURE_AND_RETHROW( (edge_type) ) }

const graph_edge_property_object* database::find_graph_edge_property( const graph_edge_name_type& edge_type )const
{
   return find< graph_edge_property_object, by_edge_type >( edge_type );
}

const asset_liquidity_pool_object& database::get_liquidity_pool( const asset_symbol_type& symbol_a, const asset_symbol_type& symbol_b )const
{ try {
   return get< asset_liquidity_pool_object, by_asset_pair >( boost::make_tuple( symbol_a, symbol_b ) );
} FC_CAPTURE_AND_RETHROW( (symbol_a)(symbol_b) ) }

const asset_liquidity_pool_object* database::find_liquidity_pool( const asset_symbol_type& symbol_a, const asset_symbol_type& symbol_b )const
{
   return find< asset_liquidity_pool_object, by_asset_pair >( boost::make_tuple( symbol_a, symbol_b ) );
}

const asset_liquidity_pool_object& database::get_liquidity_pool( const asset_symbol_type& symbol )const
{ try {
   return get< asset_liquidity_pool_object, by_symbol_liquid >( symbol );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_liquidity_pool_object* database::find_liquidity_pool( const asset_symbol_type& symbol )const
{
   return find< asset_liquidity_pool_object, by_symbol_liquid >( symbol );
}

const asset_credit_pool_object& database::get_credit_pool( const asset_symbol_type& symbol, bool credit_asset )const
{ try {
   if( credit_asset)
   {
      return get< asset_credit_pool_object, by_credit_symbol >( symbol );
   }
   else
   {
      return get< asset_credit_pool_object, by_base_symbol >( symbol );
   }
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_credit_pool_object* database::find_credit_pool( const asset_symbol_type& symbol, bool credit_asset )const
{
   if( credit_asset )
   {
      return find< asset_credit_pool_object, by_credit_symbol >( symbol );
   }
   else
   {
      return find< asset_credit_pool_object, by_base_symbol >( symbol );
   }
}

const credit_collateral_object& database::get_collateral( const account_name_type& owner, const asset_symbol_type& symbol  )const
{ try {
   return get< credit_collateral_object, by_owner_symbol >( boost::make_tuple( owner, symbol ) );
} FC_CAPTURE_AND_RETHROW( (owner)(symbol) ) }

const credit_collateral_object* database::find_collateral( const account_name_type& owner, const asset_symbol_type& symbol )const
{
   return find< credit_collateral_object, by_owner_symbol >( boost::make_tuple( owner, symbol ) );
}

const credit_loan_object& database::get_loan( const account_name_type& owner, const shared_string& loan_id  )const
{ try {
   return get< credit_loan_object, by_loan_id >( boost::make_tuple( owner, loan_id ) );
} FC_CAPTURE_AND_RETHROW( (owner)(loan_id) ) }

const credit_loan_object* database::find_loan( const account_name_type& owner, const shared_string& loan_id )const
{
   return find< credit_loan_object, by_loan_id >( boost::make_tuple( owner, loan_id ) );
}

const credit_loan_object& database::get_loan( const account_name_type& owner, const string& loan_id  )const
{ try {
   return get< credit_loan_object, by_loan_id >( boost::make_tuple( owner, loan_id ) );
} FC_CAPTURE_AND_RETHROW( (owner)(loan_id) ) }

const credit_loan_object* database::find_loan( const account_name_type& owner, const string& loan_id )const
{
   return find< credit_loan_object, by_loan_id >( boost::make_tuple( owner, loan_id ) );
}

const asset_option_pool_object& database::get_option_pool( const asset_symbol_type& base_symbol, const asset_symbol_type& quote_symbol )const
{ try {
   return get< asset_option_pool_object, by_asset_pair >( boost::make_tuple( base_symbol, quote_symbol ) );
} FC_CAPTURE_AND_RETHROW( (base_symbol)(quote_symbol) ) }

const asset_option_pool_object* database::find_option_pool( const asset_symbol_type& base_symbol, const asset_symbol_type& quote_symbol )const
{
   return find< asset_option_pool_object, by_asset_pair >( boost::make_tuple( base_symbol, quote_symbol ) );
}

const asset_option_pool_object& database::get_option_pool( const asset_symbol_type& symbol )const
{ try {
   return get< asset_option_pool_object, by_asset_pair >( boost::make_tuple( SYMBOL_COIN, symbol ) );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_option_pool_object* database::find_option_pool( const asset_symbol_type& symbol )const
{
   return find< asset_option_pool_object, by_asset_pair >( boost::make_tuple( SYMBOL_COIN, symbol ) );
}

const asset_prediction_pool_object& database::get_prediction_pool( const asset_symbol_type& symbol )const
{ try {
   return get< asset_prediction_pool_object, by_prediction_symbol >( symbol );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_prediction_pool_object* database::find_prediction_pool( const asset_symbol_type& symbol )const
{
   return find< asset_prediction_pool_object, by_prediction_symbol >( symbol );
}

const asset_prediction_pool_resolution_object& database::get_prediction_pool_resolution( const account_name_type& name, const asset_symbol_type& symbol )const
{ try {
   return get< asset_prediction_pool_resolution_object, by_account >( boost::make_tuple( name, symbol ) );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_prediction_pool_resolution_object* database::find_prediction_pool_resolution( const account_name_type& name, const asset_symbol_type& symbol )const
{
   return find< asset_prediction_pool_resolution_object, by_account >( boost::make_tuple( name, symbol ) );
}

const product_sale_object& database::get_product_sale( const account_name_type& name, const shared_string& product_id )const
{ try {
   return get< product_sale_object, by_product_id >( boost::make_tuple( name, product_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(product_id) ) }

const product_sale_object* database::find_product_sale( const account_name_type& name, const shared_string& product_id )const
{
   return find< product_sale_object, by_product_id >( boost::make_tuple( name, product_id ) );
}

const product_sale_object& database::get_product_sale( const account_name_type& name, const string& product_id )const
{ try {
   return get< product_sale_object, by_product_id >( boost::make_tuple( name, product_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(product_id) ) }

const product_sale_object* database::find_product_sale( const account_name_type& name, const string& product_id )const
{
   return find< product_sale_object, by_product_id >( boost::make_tuple( name, product_id ) );
}

const product_purchase_object& database::get_product_purchase( const account_name_type& name, const shared_string& order_id )const
{ try {
   return get< product_purchase_object, by_order_id >( boost::make_tuple( name, order_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(order_id) ) }

const product_purchase_object* database::find_product_purchase( const account_name_type& name, const shared_string& order_id )const
{
   return find< product_purchase_object, by_order_id >( boost::make_tuple( name, order_id ) );
}

const product_purchase_object& database::get_product_purchase( const account_name_type& name, const string& order_id )const
{ try {
   return get< product_purchase_object, by_order_id >( boost::make_tuple( name, order_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(order_id) ) }

const product_purchase_object* database::find_product_purchase( const account_name_type& name, const string& order_id )const
{
   return find< product_purchase_object, by_order_id >( boost::make_tuple( name, order_id ) );
}

const product_auction_sale_object& database::get_product_auction_sale( const account_name_type& name, const shared_string& auction_id )const
{ try {
   return get< product_auction_sale_object, by_auction_id >( boost::make_tuple( name, auction_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(auction_id) ) }

const product_auction_sale_object* database::find_product_auction_sale( const account_name_type& name, const shared_string& auction_id )const
{
   return find< product_auction_sale_object, by_auction_id >( boost::make_tuple( name, auction_id ) );
}

const product_auction_sale_object& database::get_product_auction_sale( const account_name_type& name, const string& auction_id )const
{ try {
   return get< product_auction_sale_object, by_auction_id >( boost::make_tuple( name, auction_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(auction_id) ) }

const product_auction_sale_object* database::find_product_auction_sale( const account_name_type& name, const string& auction_id )const
{
   return find< product_auction_sale_object, by_auction_id >( boost::make_tuple( name, auction_id ) );
}

const product_auction_bid_object& database::get_product_auction_bid( const account_name_type& name, const shared_string& bid_id )const
{ try {
   return get< product_auction_bid_object, by_bid_id >( boost::make_tuple( name, bid_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(bid_id) ) }

const product_auction_bid_object* database::find_product_auction_bid( const account_name_type& name, const shared_string& bid_id )const
{
   return find< product_auction_bid_object, by_bid_id >( boost::make_tuple( name, bid_id ) );
}

const product_auction_bid_object& database::get_product_auction_bid( const account_name_type& name, const string& bid_id )const
{ try {
   return get< product_auction_bid_object, by_bid_id >( boost::make_tuple( name, bid_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(bid_id) ) }

const product_auction_bid_object* database::find_product_auction_bid( const account_name_type& name, const string& bid_id )const
{
   return find< product_auction_bid_object, by_bid_id >( boost::make_tuple( name, bid_id ) );
}

const escrow_object& database::get_escrow( const account_name_type& name, const shared_string& escrow_id )const
{ try {
   return get< escrow_object, by_from_id >( boost::make_tuple( name, escrow_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(escrow_id) ) }

const escrow_object* database::find_escrow( const account_name_type& name, const shared_string& escrow_id )const
{
   return find< escrow_object, by_from_id >( boost::make_tuple( name, escrow_id ) );
}

const escrow_object& database::get_escrow( const account_name_type& name, const string& escrow_id )const
{ try {
   return get< escrow_object, by_from_id >( boost::make_tuple( name, escrow_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(escrow_id) ) }

const escrow_object* database::find_escrow( const account_name_type& name, const string& escrow_id )const
{
   return find< escrow_object, by_from_id >( boost::make_tuple( name, escrow_id ) );
}

const transfer_request_object& database::get_transfer_request( const account_name_type& name, const shared_string& request_id )const
{ try {
   return get< transfer_request_object, by_request_id >( boost::make_tuple( name, request_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(request_id) ) }

const transfer_request_object* database::find_transfer_request( const account_name_type& name, const shared_string& request_id )const
{
   return find< transfer_request_object, by_request_id >( boost::make_tuple( name, request_id ) );
}

const transfer_request_object& database::get_transfer_request( const account_name_type& name, const string& request_id )const
{ try {
   return get< transfer_request_object, by_request_id >( boost::make_tuple( name, request_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(request_id) ) }

const transfer_request_object* database::find_transfer_request( const account_name_type& name, const string& request_id )const
{
   return find< transfer_request_object, by_request_id >( boost::make_tuple( name, request_id ) );
}

const transfer_recurring_object& database::get_transfer_recurring( const account_name_type& name, const shared_string& transfer_id )const
{ try {
   return get< transfer_recurring_object, by_transfer_id >( boost::make_tuple( name, transfer_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(transfer_id) ) }

const transfer_recurring_object* database::find_transfer_recurring( const account_name_type& name, const shared_string& transfer_id )const
{
   return find< transfer_recurring_object, by_transfer_id >( boost::make_tuple( name, transfer_id ) );
}

const transfer_recurring_object& database::get_transfer_recurring( const account_name_type& name, const string& transfer_id )const
{ try {
   return get< transfer_recurring_object, by_transfer_id >( boost::make_tuple( name, transfer_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(transfer_id) ) }

const transfer_recurring_object* database::find_transfer_recurring( const account_name_type& name, const string& transfer_id )const
{
   return find< transfer_recurring_object, by_transfer_id >( boost::make_tuple( name, transfer_id ) );
}

const transfer_recurring_request_object& database::get_transfer_recurring_request( const account_name_type& name, const shared_string& request_id )const
{ try {
   return get< transfer_recurring_request_object, by_request_id >( boost::make_tuple( name, request_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(request_id) ) }

const transfer_recurring_request_object* database::find_transfer_recurring_request( const account_name_type& name, const shared_string& request_id )const
{
   return find< transfer_recurring_request_object, by_request_id >( boost::make_tuple( name, request_id ) );
}

const transfer_recurring_request_object& database::get_transfer_recurring_request( const account_name_type& name, const string& request_id )const
{ try {
   return get< transfer_recurring_request_object, by_request_id >( boost::make_tuple( name, request_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(request_id) ) }

const transfer_recurring_request_object* database::find_transfer_recurring_request( const account_name_type& name, const string& request_id )const
{
   return find< transfer_recurring_request_object, by_request_id >( boost::make_tuple( name, request_id ) );
}

const limit_order_object& database::get_limit_order( const account_name_type& name, const shared_string& order_id )const
{ try {
   return get< limit_order_object, by_account >( boost::make_tuple( name, order_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(order_id) ) }

const limit_order_object* database::find_limit_order( const account_name_type& name, const shared_string& order_id )const
{
   return find< limit_order_object, by_account >( boost::make_tuple( name, order_id ) );
}

const limit_order_object& database::get_limit_order( const account_name_type& name, const string& order_id )const
{ try {
   return get< limit_order_object, by_account >( boost::make_tuple( name, order_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(order_id) ) }

const limit_order_object* database::find_limit_order( const account_name_type& name, const string& order_id )const
{
   return find< limit_order_object, by_account >( boost::make_tuple( name, order_id ) );
}

const margin_order_object& database::get_margin_order( const account_name_type& name, const shared_string& margin_id )const
{ try {
   return get< margin_order_object, by_account >( boost::make_tuple( name, margin_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(margin_id) ) }

const margin_order_object* database::find_margin_order( const account_name_type& name, const shared_string& margin_id )const
{
   return find< margin_order_object, by_account >( boost::make_tuple( name, margin_id ) );
}

const margin_order_object& database::get_margin_order( const account_name_type& name, const string& margin_id )const
{ try {
   return get< margin_order_object, by_account >( boost::make_tuple( name, margin_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(margin_id) ) }

const margin_order_object* database::find_margin_order( const account_name_type& name, const string& margin_id )const
{
   return find< margin_order_object, by_account >( boost::make_tuple( name, margin_id ) );
}

const option_order_object& database::get_option_order( const account_name_type& name, const shared_string& option_id )const
{ try {
   return get< option_order_object, by_account >( boost::make_tuple( name, option_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(option_id) ) }

const option_order_object* database::find_option_order( const account_name_type& name, const shared_string& option_id )const
{
   return find< option_order_object, by_account >( boost::make_tuple( name, option_id ) );
}

const option_order_object& database::get_option_order( const account_name_type& name, const string& option_id )const
{ try {
   return get< option_order_object, by_account >( boost::make_tuple( name, option_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(option_id) ) }

const option_order_object* database::find_option_order( const account_name_type& name, const string& option_id )const
{
   return find< option_order_object, by_account >( boost::make_tuple( name, option_id ) );
}

const auction_order_object& database::get_auction_order( const account_name_type& name, const shared_string& auction_id )const
{ try {
   return get< auction_order_object, by_account >( boost::make_tuple( name, auction_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(auction_id) ) }

const auction_order_object* database::find_auction_order( const account_name_type& name, const shared_string& auction_id )const
{
   return find< auction_order_object, by_account >( boost::make_tuple( name, auction_id ) );
}

const auction_order_object& database::get_auction_order( const account_name_type& name, const string& auction_id )const
{ try {
   return get< auction_order_object, by_account >( boost::make_tuple( name, auction_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(auction_id) ) }

const auction_order_object* database::find_auction_order( const account_name_type& name, const string& auction_id )const
{
   return find< auction_order_object, by_account >( boost::make_tuple( name, auction_id ) );
}

const call_order_object& database::get_call_order( const account_name_type& name, const asset_symbol_type& symbol )const
{ try {
   return get< call_order_object, by_account >( boost::make_tuple( name, symbol ) );
} FC_CAPTURE_AND_RETHROW( (name)(symbol) ) }

const call_order_object* database::find_call_order( const account_name_type& name, const asset_symbol_type& symbol )const
{
   return find< call_order_object, by_account >( boost::make_tuple( name, symbol ) );
}

const asset_collateral_bid_object& database::get_asset_collateral_bid( const account_name_type& name, const asset_symbol_type& symbol )const
{ try {
   return get< asset_collateral_bid_object, by_account >( boost::make_tuple( name, symbol ) );
} FC_CAPTURE_AND_RETHROW( (name)(symbol) ) }

const asset_collateral_bid_object* database::find_asset_collateral_bid( const account_name_type& name, const asset_symbol_type& symbol )const
{
   return find< asset_collateral_bid_object, by_account >( boost::make_tuple( name, symbol ) );
}

const asset_settlement_object& database::get_asset_settlement( const account_name_type& name, const asset_symbol_type& symbol )const
{ try {
   return get< asset_settlement_object, by_account_asset >( boost::make_tuple( name, symbol ) );
} FC_CAPTURE_AND_RETHROW( (name)(symbol) ) }

const asset_settlement_object* database::find_asset_settlement( const account_name_type& name, const asset_symbol_type& symbol )const
{
   return find< asset_settlement_object, by_account_asset >( boost::make_tuple( name, symbol ) );
}

const asset_distribution_object& database::get_asset_distribution( const asset_symbol_type& symbol )const
{ try {
   return get< asset_distribution_object, by_symbol >( symbol );
} FC_CAPTURE_AND_RETHROW( (symbol) ) }

const asset_distribution_object* database::find_asset_distribution( const asset_symbol_type& symbol )const
{
   return find< asset_distribution_object, by_symbol >( symbol );
}

const asset_distribution_balance_object& database::get_asset_distribution_balance( const account_name_type& name, const asset_symbol_type& symbol )const
{ try {
   return get< asset_distribution_balance_object, by_account_distribution >( boost::make_tuple( name, symbol ) );
} FC_CAPTURE_AND_RETHROW( (name)(symbol) ) }

const asset_distribution_balance_object* database::find_asset_distribution_balance( const account_name_type& name, const asset_symbol_type& symbol )const
{
   return find< asset_distribution_balance_object, by_account_distribution >( boost::make_tuple( name, symbol ) );
}

const savings_withdraw_object& database::get_savings_withdraw( const account_name_type& owner, const shared_string& request_id )const
{ try {
   return get< savings_withdraw_object, by_request_id >( boost::make_tuple( owner, request_id ) );
} FC_CAPTURE_AND_RETHROW( (owner)(request_id) ) }

const savings_withdraw_object* database::find_savings_withdraw( const account_name_type& owner, const shared_string& request_id )const
{
   return find< savings_withdraw_object, by_request_id >( boost::make_tuple( owner, request_id ) );
}

const savings_withdraw_object& database::get_savings_withdraw( const account_name_type& owner, const string& request_id )const
{ try {
   return get< savings_withdraw_object, by_request_id >( boost::make_tuple( owner, request_id ) );
} FC_CAPTURE_AND_RETHROW( (owner)(request_id) ) }

const savings_withdraw_object* database::find_savings_withdraw( const account_name_type& owner, const string& request_id )const
{
   return find< savings_withdraw_object, by_request_id >( boost::make_tuple( owner, request_id ) );
}

const reward_fund_object& database::get_reward_fund( const asset_symbol_type& symbol ) const
{ try {
   return get< reward_fund_object, by_symbol >( symbol );
} FC_CAPTURE_AND_RETHROW( (symbol)) }

const reward_fund_object* database::find_reward_fund( const asset_symbol_type& symbol ) const
{ try {
   return find< reward_fund_object, by_symbol >( symbol );
} FC_CAPTURE_AND_RETHROW( (symbol)) }

const comment_metrics_object& database::get_comment_metrics() const
{ try {
   return get< comment_metrics_object>();
} FC_CAPTURE_AND_RETHROW() }

const transaction_id_type& database::get_current_transaction_id()const
{
   return _current_trx_id;
}

uint16_t database::get_current_op_in_trx()const
{
   return _current_op_in_trx;
}

asset database::asset_to_USD( const price& p, const asset& a ) const
{
   FC_ASSERT( a.symbol != SYMBOL_USD );
   asset_symbol_type quote_symbol = p.quote.symbol;
   asset_symbol_type base_symbol = p.base.symbol;
   FC_ASSERT( base_symbol == SYMBOL_USD || quote_symbol == SYMBOL_USD );
   asset value_usd = asset( 0, SYMBOL_USD);

   if( p.is_null() )
   {
      return value_usd;
   }
   else
   {
      value_usd = a * p;
      return value_usd;
   }
}


asset database::asset_to_USD( const asset& a ) const
{
   price usd_price = get_usd_price();
   asset coin_value = a;
   asset usd_value = asset( 0, SYMBOL_USD );
   
   if( a.symbol != SYMBOL_COIN )
   {
      price coin_price = get_liquidity_pool( SYMBOL_COIN, a.symbol ).current_price();
      usd_value = asset_to_USD( usd_price, coin_value * coin_price );
   }
   else
   {
      usd_value = asset_to_USD( usd_price, coin_value );
   }
   return usd_value;
}


asset database::USD_to_asset( const price& p, const asset& a ) const
{
   FC_ASSERT( a.symbol == SYMBOL_USD );
   asset_symbol_type quote_symbol = p.quote.symbol;
   asset_symbol_type base_symbol = p.base.symbol;
   FC_ASSERT( base_symbol == SYMBOL_USD || quote_symbol == SYMBOL_USD );

   asset usd_value = a;
   asset coin_value = asset( 0, SYMBOL_USD );
   price coin_price = p;

   if( p.is_null() ) 
   {
      if( base_symbol == SYMBOL_USD ) 
      {
         coin_value = asset( 0, quote_symbol );
      } 
      else if( quote_symbol == SYMBOL_USD ) 
      {
         coin_value = asset( 0, base_symbol );
      }
   }
   else
   {
      coin_value = usd_value * coin_price;
   }
   
   return coin_value;
}


asset database::USD_to_asset( const asset& a ) const
{
   price usd_price = get_usd_price();
   return USD_to_asset( usd_price, a );
}

/**
 * Returns the asset value of the comment reward
 * from a specified comment reward context.
 */
asset database::get_comment_reward( const comment_object& c, const util::comment_reward_context& ctx ) const
{ try {
   FC_ASSERT( c.net_reward.value > 0 );
   FC_ASSERT( ctx.recent_content_claims > 0 );
   FC_ASSERT( ctx.total_reward_fund.amount.value > 0 );

   uint256_t rf = util::to256( uint128_t( ctx.total_reward_fund.amount.value ) );
   uint256_t total_claims = util::to256( ctx.recent_content_claims );
   uint128_t reward_curve = util::evaluate_reward_curve( c );
   uint256_t claim = util::to256( reward_curve );

   uint256_t payout_uint256 = ( rf * claim ) / total_claims;
   FC_ASSERT( payout_uint256 <= util::to256( uint128_t( std::numeric_limits<int64_t>::max() ) ) );
   share_type payout = static_cast< int64_t >( payout_uint256 );
   asset reward_value = asset( payout, ctx.total_reward_fund.symbol );

   if( reward_value * ctx.current_COIN_USD_price < MIN_PAYOUT_USD )
   {
      payout = 0;
   }

   asset max_reward_coin = c.max_accepted_payout * ctx.current_COIN_USD_price;
   payout = std::min( payout, share_type( max_reward_coin.amount.value ) );
   reward_value = asset( payout, ctx.total_reward_fund.symbol );

   FC_ASSERT( reward_value.amount <= ctx.total_reward_fund.amount,
      "Reward Value: ${v} is greater than total reward fund: ${f}",
      ("v",reward_value.to_string())("f",ctx.total_reward_fund.to_string()));

   return reward_value;

} FC_CAPTURE_AND_RETHROW( (c)(ctx) ) }


const hardfork_property_object& database::get_hardfork_property_object()const
{ try {
   return get< hardfork_property_object >();
} FC_CAPTURE_AND_RETHROW() }


const time_point database::calculate_discussion_payout_time( const comment_object& comment )const
{
   return comment.cashout_time;
}

/**
 * Returns a Shuffled copy of a specified vector of accounts
 * High performance random generator using 256 bits of internal state.
 * http://xorshift.di.unimi.it/
 */
vector< account_name_type > database::shuffle_accounts( vector< account_name_type > accounts )const
{
   vector< account_name_type > set = accounts;
   auto now_hi = uint64_t( head_block_time().time_since_epoch().count() ) << 32;
   for( uint64_t i = 0; i < accounts.size(); ++i )
   {
      uint64_t k = now_hi +      uint64_t(i)*2685757105773633871ULL;
      uint64_t l = ( now_hi >> 1 ) + uint64_t(i)*9519819187187829351ULL;
      uint64_t m = ( now_hi >> 2 ) + uint64_t(i)*5891972902484196198ULL;
      uint64_t n = ( now_hi >> 3 ) + uint64_t(i)*2713716410970705441ULL;
      
      k ^= (l >> 7);
      l ^= (m << 9);
      m ^= (n >> 5);
      n ^= (k << 3);

      k*= 1422657256589674161ULL;
      l*= 9198587865873687103ULL;
      m*= 3060558831167252908ULL;
      n*= 4306921374257631524ULL;

      k ^= (l >> 2);
      l ^= (m << 4);
      m ^= (n >> 1);
      n ^= (k << 9);

      k*= 7947775653275249570ULL;
      l*= 9490802558828203479ULL;
      m*= 2694198061645862341ULL;
      n*= 3190223686201138213ULL;

      uint64_t rand = (k ^ l) ^ (m ^ n) ; 
      uint64_t max = set.size() - i;

      uint64_t j = i + rand % max;
      std::swap( set[i], set[j] );
   }
   return set;
};

uint32_t database::producer_participation_rate()const
{
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   return uint64_t(PERCENT_100) * props.recent_slots_filled.popcount() / 128;
}

void database::add_checkpoints( const flat_map< uint64_t, block_id_type >& checkpts )
{
   for( const auto& i : checkpts )
      _checkpoints[i.first] = i.second;
}

bool database::before_last_checkpoint()const
{
   return (_checkpoints.size() > 0) && (_checkpoints.rbegin()->first >= head_block_num());
}

/**
 * Push block "may fail" in which case every partial change is unwound.  After
 * push block is successful the block is appended to the chain database on disk.
 *
 * @return true if we switched forks as a result of this push.
 */
bool database::push_block( const signed_block& new_block, uint32_t skip )
{
   fc::time_point begin_time = fc::time_point::now();

   bool result;
   detail::with_skip_flags( *this, skip, [&]()
   {
      with_write_lock( [&]()
      {
         detail::without_pending_transactions( *this, std::move(_pending_tx), [&]()
         {
            try
            {
               result = _push_block(new_block);
            }
            FC_CAPTURE_AND_RETHROW( (new_block) )
         });
      });
   });

   fc::time_point end_time = fc::time_point::now();
   fc::microseconds dt = end_time - begin_time;
   if( ( new_block.block_num() % 10000 ) == 0 )
   {
      ilog( "Push_block ${b} took ${t} microseconds", ("b", new_block.block_num())("t", dt.count()) );
   }
 
   return result;
}

void database::_maybe_warn_multiple_production( uint64_t height )const
{
   vector< item_ptr > blocks = _fork_db.fetch_block_by_number( height );

   if( blocks.size() > 1 )
   {
      vector< signed_block > block_list;

      for( const auto& b : blocks )
      {
         block_list.push_back( b->data );
      }

      ilog( "Encountered block num collision at block ${n} due to a fork.", ("n", height) );
   }
   return;
}

bool database::_push_block( const signed_block& new_block )
{ try {
   uint32_t skip = get_node_properties().skip_flags;
   // uint32_t skip_undo_db = skip & skip_undo_block;

   if( !(skip&skip_fork_db) )
   {
      shared_ptr< fork_item > new_head = _fork_db.push_block( new_block );
      _maybe_warn_multiple_production( new_head->num );

      // If the head block from the longest chain does not build off of the current head, we need to switch forks.

      if( new_head->data.previous != head_block_id() )
      {
         // If the newly pushed block is the same height as head, we get head back in new_head
         // Only switch forks if new_head is actually higher than head

         if( new_head->data.block_num() > head_block_num() )
         {
            wlog( "Switching to fork: ${id}", ("id",new_head->data.id()) );
            auto branches = _fork_db.fetch_branch_from( new_head->data.id(), head_block_id() );

            // pop blocks until we hit the forked block
            while( head_block_id() != branches.second.back()->data.previous )
            { 
               pop_block();
            }

            // push all blocks on the new fork
            for( auto ritr = branches.first.rbegin(); ritr != branches.first.rend(); ++ritr )
            {
               ilog( "Pushing blocks from fork ${n} ${id}", ("n",(*ritr)->data.block_num())("id",(*ritr)->data.id()) );
               optional<fc::exception> except;
               try
               {
                  auto session = start_undo_session( true );
                  apply_block( (*ritr)->data, skip );
                  session.push();
               }
               catch ( const fc::exception& e ) { except = e; }

               if( except )
               {
                  wlog( "exception thrown while switching forks ${e}", ("e",except->to_detail_string() ) );
                  // remove the rest of branches.first from the fork_db, those blocks are invalid
                  while( ritr != branches.first.rend() )
                  {
                     _fork_db.remove( (*ritr)->data.id() );
                     ++ritr;
                  }
                  _fork_db.set_head( branches.second.front() );

                  // pop all blocks from the bad fork
                  while( head_block_id() != branches.second.back()->data.previous )
                  {
                     pop_block();
                  }
                  // restore all blocks from the good fork
                  for( auto ritr = branches.second.rbegin(); ritr != branches.second.rend(); ++ritr )
                  {
                     auto session = start_undo_session( true );
                     apply_block( (*ritr)->data, skip );
                     session.push();
                  }
                  throw *except;
               }
            }
            return true;
         }
         else
         {
            return false;
         }
      }
   }

   try
   {
      auto session = start_undo_session( true );
      apply_block( new_block, skip );
      session.push();
   }
   catch( const fc::exception& e )
   {
      elog("Failed to push new block: \n ${e}", ("e", e.to_detail_string()));
      _fork_db.remove(new_block.id());
      throw;
   }

   return false;
} FC_CAPTURE_AND_RETHROW( ( new_block ) ) }

/**
 * Attempts to push the transaction into the pending queue
 *
 * When called to push a locally generated transaction, set the skip_block_size_check bit on the skip argument. This
 * will allow the transaction to be pushed even if it causes the pending block size to exceed the maximum block size.
 * Although the transaction will probably not propagate further now, as the peers are likely to have their pending
 * queues full as well, it will be kept in the queue to be propagated later when a new block flushes out the pending
 * queues.
 */
void database::push_transaction( const signed_transaction& trx, uint32_t skip )
{ try { try {
   const median_chain_property_object& median_props = get_median_chain_properties();

   FC_ASSERT( fc::raw::pack_size( trx ) <= ( median_props.maximum_block_size - 256 ),
      "Transaction size must be less than maximum block size." );

   set_producing( true );

   detail::with_skip_flags( *this, skip, [&]()
   {
      with_write_lock( [&]()
      {
         _push_transaction( trx );
      });
   });
   set_producing( false );
   }
   catch( ... )
   {
      set_producing( false );
      throw;
   }
}
FC_CAPTURE_AND_RETHROW( (trx) ) }

void database::_push_transaction( const signed_transaction& trx )
{
   // If this is the first transaction pushed after applying a block, start a new undo session.
   // This allows us to quickly rewind to the clean state of the head block, in case a new block arrives.
   if( !_pending_tx_session.valid() )
      _pending_tx_session = start_undo_session( true );

   // Create a temporary undo session as a child of _pending_tx_session.
   // The temporary session will be discarded by the destructor if
   // _apply_transaction fails.  If we make it to merge(), we
   // apply the changes.

   auto temp_session = start_undo_session( true );
   _apply_transaction( trx );
   _pending_tx.push_back( trx );

   // The transaction applied successfully. Merge its changes into the pending block session.
   temp_session.squash();

   // notify anyone listening to pending transactions
   notify_on_pending_transaction( trx );
}


/** 
 * Creates a new block using the keys provided to the producer node, 
 * when the producer is scheduled and syncronised.
 */
signed_block database::generate_block(
   time_point when,
   const account_name_type& producer_owner,
   const fc::ecc::private_key& block_signing_private_key,
   uint32_t skip )
{
   signed_block result;
   detail::with_skip_flags( *this, skip, [&]()
   {
      try
      {
         result = _generate_block( when, producer_owner, block_signing_private_key );
      }
      FC_CAPTURE_AND_RETHROW( (producer_owner) )
   });
   return result;
}

signed_block database::_generate_block( fc::time_point when, const account_name_type& producer_owner, 
   const fc::ecc::private_key& block_signing_private_key )
{
   uint32_t skip = get_node_properties().skip_flags;
   uint64_t slot_num = get_slot_at_time( when );
   const median_chain_property_object& median_props = get_median_chain_properties();

   FC_ASSERT( slot_num > 0,
      "Slot number must be greater than zero." );
   string scheduled_producer = get_scheduled_producer( slot_num );
   FC_ASSERT( scheduled_producer == producer_owner,
      "Scheduled producer must be the same as producer owner." );

   const producer_object& producer = get_producer( producer_owner );

   if( !(skip & skip_producer_signature) )
   {
      FC_ASSERT( producer.signing_key == block_signing_private_key.get_public_key(),
         "Block signing key must be equal to the producers block signing key." );
   }
      
   signed_block pending_block;

   pending_block.previous = head_block_id();
   pending_block.timestamp = when;
   pending_block.producer = producer_owner;
   
   auto blockchainVersion = BLOCKCHAIN_VERSION;
   if( producer.running_version != BLOCKCHAIN_VERSION )
   {
      pending_block.extensions.insert( block_header_extensions( BLOCKCHAIN_VERSION ) );
   }
      
   const hardfork_property_object& hfp = get_hardfork_property_object();

   auto blockchainHardforkVersion = BLOCKCHAIN_HARDFORK_VERSION;
   if( hfp.current_hardfork_version < BLOCKCHAIN_HARDFORK_VERSION // Binary is newer hardfork than has been applied
      && ( producer.hardfork_version_vote != _hardfork_versions[ hfp.last_hardfork + 1 ] || 
      producer.hardfork_time_vote != _hardfork_times[ hfp.last_hardfork + 1 ] ) ) // producer vote does not match binary configuration
   {
      // Make vote match binary configuration
      pending_block.extensions.insert( block_header_extensions( hardfork_version_vote( _hardfork_versions[ hfp.last_hardfork + 1 ], _hardfork_times[ hfp.last_hardfork + 1 ] ) ) );
   }
   else if( hfp.current_hardfork_version == BLOCKCHAIN_HARDFORK_VERSION     // Binary does not know of a new hardfork
      && producer.hardfork_version_vote > BLOCKCHAIN_HARDFORK_VERSION )      // Voting for hardfork in the future, that we do not know of...
   {
      // Make vote match binary configuration. This is vote to not apply the new hardfork.
      pending_block.extensions.insert( block_header_extensions( hardfork_version_vote( _hardfork_versions[ hfp.last_hardfork ], _hardfork_times[ hfp.last_hardfork ] ) ) );
   }
   
   size_t total_block_size = fc::raw::pack_size( pending_block ) + 4;       // The 4 is for the max size of the transaction vector length
   uint64_t maximum_block_size = median_props.maximum_block_size;     // MAX_BLOCK_SIZE;

   with_write_lock( [&]()
   {
      // The following code throws away existing pending_tx_session and
      // rebuilds it by re-applying pending transactions.
      // This rebuild is necessary because pending transactions' validity
      // and semantics may have changed since they were received, because
      // time-based semantics are evaluated based on the current block
      // time.  These changes can only be reflected in the database when
      // the value of the "when" variable is known, which means we need to
      // re-apply pending transactions in this method.

      _pending_tx_session.reset();
      _pending_tx_session = start_undo_session( true );

      uint64_t postponed_tx_count = 0;   // pop pending state (reset to head block state)
      
      for( const signed_transaction& tx : _pending_tx )
      {
         if( tx.expiration < when )    // Only include transactions that have not expired yet for currently generating block.
            continue;

         uint64_t new_total_size = total_block_size + fc::raw::pack_size( tx );

         if( new_total_size >= maximum_block_size )
         {
            postponed_tx_count++;     // postpone transaction if it would make block too big
            continue;
         }
         try
         {
            auto temp_session = start_undo_session( true );
            _apply_transaction( tx );
            temp_session.squash();

            total_block_size += fc::raw::pack_size( tx );
            pending_block.transactions.push_back( tx );
         }
         catch ( const fc::exception& e )      // Do nothing, transaction will not be re-applied
         {
            wlog( "Transaction was not processed while generating block due to ${e}", ("e", e) );
            wlog( "The transaction was ${t}", ("t", tx) );
         }
      }
      if( postponed_tx_count > 0 )
      {
         wlog( "Postponed ${n} transactions due to block size limit", ("n", postponed_tx_count) );
      }

      _pending_tx_session.reset();
   });

   // We have temporarily broken the invariant that
   // _pending_tx_session is the result of applying _pending_tx, as
   // _pending_tx now consists of the set of postponed transactions.
   // However, the push_block() call below will re-create the
   // _pending_tx_session.

   pending_block.transaction_merkle_root = pending_block.calculate_merkle_root();

   if( !(skip & skip_producer_signature) )
   {
      pending_block.sign( block_signing_private_key );
   }

   if( !(skip & skip_block_size_check) )
   {
      FC_ASSERT( fc::raw::pack_size( pending_block ) <= MAX_BLOCK_SIZE );
   }

   // ilog( "Generated Block ID: ${i} at block height: ${h}",("i", pending_block.id() )("h", pending_block.block_num() ) );

   push_block( pending_block, skip );

   return pending_block;
}

/**
 * Removes the most recent block from the database and undoes any changes it made.
 */
void database::pop_block()
{ try {
   ilog( "Popping Block" );

   _pending_tx_session.reset();
   auto head_id = head_block_id();

   optional<signed_block> head_block = fetch_block_by_id( head_id );   // save the head block so we can recover its transactions
   ASSERT( head_block.valid(), pop_empty_chain,
      "There are no blocks to pop." );

   _fork_db.pop_block();
   undo();
   _popped_tx.insert( _popped_tx.begin(), head_block->transactions.begin(), head_block->transactions.end() );

} FC_CAPTURE_AND_RETHROW() }

void database::clear_pending()
{ try {
   assert( (_pending_tx.size() == 0) || _pending_tx_session.valid() );
   _pending_tx.clear();
   _pending_tx_session.reset();
} FC_CAPTURE_AND_RETHROW() }

void database::notify_pre_apply_operation( operation_notification& note )
{
   note.trx_id       = _current_trx_id;
   note.block        = _current_block_num;
   note.trx_in_block = _current_trx_in_block;
   note.op_in_trx    = _current_op_in_trx;

   TRY_NOTIFY( pre_apply_operation, note )
}

void database::notify_post_apply_operation( const operation_notification& note )
{
   TRY_NOTIFY( post_apply_operation, note )
}

void database::push_virtual_operation( const operation& op, bool force )
{
   if( !force )
   {
      #if defined( IS_LOW_MEM ) && ! defined( IS_TEST_NET )
      return;
      #endif
   }
   FC_ASSERT( is_virtual_operation( op ) );
   operation_notification note(op);
   notify_pre_apply_operation( note );
   notify_post_apply_operation( note );
}

void database::notify_applied_block( const signed_block& block )
{
   TRY_NOTIFY( applied_block, block )
}

void database::notify_pre_apply_block( const signed_block& block )
{
   TRY_NOTIFY( pre_apply_block, block )
}

void database::notify_on_pending_transaction( const signed_transaction& tx )
{
   TRY_NOTIFY( on_pending_transaction, tx )
}

void database::notify_on_pre_apply_transaction( const signed_transaction& tx )
{
   TRY_NOTIFY( on_pre_apply_transaction, tx )
}

void database::notify_on_applied_transaction( const signed_transaction& tx )
{
   TRY_NOTIFY( on_applied_transaction, tx )
}

account_name_type database::get_scheduled_producer( uint64_t slot_num )const
{
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   const producer_schedule_object& pso = get_producer_schedule();
   uint64_t current_aslot = props.current_aslot + slot_num;
   account_name_type scheduled_producer = pso.current_shuffled_producers[ current_aslot % pso.num_scheduled_producers ];
   return scheduled_producer;
}

fc::time_point database::get_slot_time( uint64_t slot_num )const
{
   if( slot_num == 0 )
   {
      return fc::time_point();
   }

   int64_t interval = BLOCK_INTERVAL.count();

   const dynamic_global_property_object& dgpo = get_dynamic_global_properties();

   if( dgpo.head_block_number == 0 )
   {
      // n.b. first block is at genesis_time plus one block interval
      fc::time_point genesis_time = dgpo.time;
      return genesis_time + fc::microseconds( slot_num * interval );
   }

   // "slot 0" is head_slot_time
   // "slot 1" is head_slot_time

   int64_t head_block_abs_slot = dgpo.time.time_since_epoch().count() / interval;
   fc::time_point head_slot_time = fc::time_point( fc::microseconds( head_block_abs_slot * interval ) );
   fc::time_point slot_time = head_slot_time + fc::microseconds( slot_num * interval );
   return slot_time;
}

uint64_t database::get_slot_at_time( fc::time_point when )const
{
   fc::time_point first_slot_time = get_slot_time( 1 );

   if( when < first_slot_time ) 
   {
      return 0;
   }

   uint64_t slot_number = ( ( when - first_slot_time ).count() / BLOCK_INTERVAL.count() ) + 1;
   return slot_number;
}

void database::update_producer_set()
{ try {
   if( (head_block_num() % SET_UPDATE_BLOCK_INTERVAL) != 0 )    // Runs once per day
      return;

   process_update_producer_set();

} FC_CAPTURE_AND_RETHROW() }


void database::process_update_producer_set()
{ try {
   const producer_schedule_object& pso = get_producer_schedule();
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   const median_chain_property_object& median_props = get_median_chain_properties();
   const auto& producer_idx = get_index< producer_index >().indices().get< by_voting_power >();
   auto producer_itr = producer_idx.begin();
   uint128_t total_producer_voting_power = 0;
   
   while( producer_itr != producer_idx.end() )
   {
      total_producer_voting_power += update_producer( *producer_itr, pso, props, median_props ).value;
      ++producer_itr;
   }

   modify( pso, [&]( producer_schedule_object& pso )
   {
      pso.total_producer_voting_power = total_producer_voting_power;
   });

   // ilog( "Updated Producer set." );

} FC_CAPTURE_AND_RETHROW() }


/**
 * Updates the voting power and vote count of a producer
 * and returns the total voting power supporting the producer.
 */
share_type database::update_producer( const producer_object& producer, const producer_schedule_object& pso,
   const dynamic_global_property_object& props, const median_chain_property_object& median_props )
{ try {
   const auto& producer_vote_idx = get_index< producer_vote_index >().indices().get< by_producer_account >();
   auto producer_vote_itr = producer_vote_idx.lower_bound( producer.owner );
   price equity_price = props.current_median_equity_price;
   time_point now = head_block_time();
   share_type voting_power = 0;
   uint32_t vote_count = 0;

   while( producer_vote_itr != producer_vote_idx.end() && 
      producer_vote_itr->producer == producer.owner )
   {
      const producer_vote_object& vote = *producer_vote_itr;
      const account_object& voter = get_account( vote.account );
      share_type weight = get_voting_power( producer_vote_itr->account, equity_price );
      if( voter.proxied.size() )
      {
         weight += get_proxied_voting_power( voter, equity_price );
      }
      // divides voting weight by 2^vote_rank, limiting total voting weight -> total voting power as votes increase.
      voting_power += share_type( weight.value >> vote.vote_rank );
      vote_count++;
      ++producer_vote_itr;
   }

   modify( producer, [&]( producer_object& p )
   {
      p.voting_power = voting_power;
      p.vote_count = vote_count;
      auto delta_pos = p.voting_power.value * (pso.current_voting_virtual_time - p.voting_virtual_last_update);
      p.voting_virtual_position += delta_pos;
      p.voting_virtual_scheduled_time = p.voting_virtual_last_update + (VIRTUAL_SCHEDULE_LAP_LENGTH - p.voting_virtual_position)/(p.voting_power.value+1);
      /** producers with a low number of votes could overflow the time field and end up with a scheduled time in the past */
      if( p.voting_virtual_scheduled_time < pso.current_voting_virtual_time ) 
      {
         p.voting_virtual_scheduled_time = fc::uint128::max_value();
      }
      p.decay_weights( now, median_props );
      p.voting_virtual_last_update = pso.current_voting_virtual_time;
   });

   return voting_power;

} FC_CAPTURE_AND_RETHROW() }


/**
 * Updates the voting power map of the moderators
 * in a community, which determines the distribution of the
 * moderation rewards for the community.
 */
void database::update_community_moderators( const community_member_object& community )
{ try {
   ilog( "Update Community moderators: ${c}", ("c", community.name) );
   price equity_price = get_liquidity_pool( SYMBOL_COIN, SYMBOL_EQUITY ).hour_median_price;
   const auto& vote_idx = get_index< community_moderator_vote_index >().indices().get< by_community_moderator >();
   flat_map< account_name_type, share_type > mod_weight;
   share_type total = 0;
   auto vote_itr = vote_idx.lower_bound( community.name );

   while( vote_itr != vote_idx.end() && 
      vote_itr->community == community.name )
   {
      const community_moderator_vote_object& vote = *vote_itr;
      const account_object& voter = get_account( vote.account );
      share_type weight = get_voting_power( vote.account );
      
      if( voter.proxied.size() )
      {
         weight += get_proxied_voting_power( voter, equity_price );
      }
      share_type w = share_type( weight.value >> vote.vote_rank );

      // divides voting weight by 2^vote_rank, limiting total voting weight -> total voting power as votes increase.

      mod_weight[ vote.moderator ] += w;
      total += w;
      ++vote_itr;
   }
   
   modify( community, [&]( community_member_object& b )
   {
      b.mod_weight = mod_weight;
      b.total_mod_weight = total;
   });

} FC_CAPTURE_AND_RETHROW() }


/**
 * Updates the voting power map of the moderators
 * in a community, which determines the distribution of
 * moderation rewards for the community.
 */
void database::update_community_moderator_set()
{ try {
   if( (head_block_num() % SET_UPDATE_BLOCK_INTERVAL) != 0 )    // Runs once per day
      return;
   
   const auto& community_idx = get_index< community_member_index >().indices().get< by_name >();
   auto community_itr = community_idx.begin();

   while( community_itr != community_idx.end() )
   {
      update_community_moderators( *community_itr );
      ++community_itr;
   }
} FC_CAPTURE_AND_RETHROW() }

/**
 * Updates the voting statistics, executive board, and officer set of a business
 * account.
 */
void database::update_business_account( const account_business_object& business )
{ try {
   const auto& bus_officer_vote_idx = get_index< account_officer_vote_index >().indices().get< by_business_account_rank >();
   const auto& bus_executive_vote_idx = get_index< account_executive_vote_index >().indices().get< by_business_role_executive >();

   flat_map< account_name_type, share_type > officers;
   flat_map< account_name_type, flat_map< executive_role_type, share_type > > exec_map;
   vector< pair< account_name_type, pair< executive_role_type, share_type > > > role_rank;
   
   role_rank.reserve( executive_role_values.size() * officers.size() );
   flat_map< account_name_type, pair< executive_role_type, share_type > > executives;
   executive_officer_set exec_set;

   auto bus_officer_vote_itr = bus_officer_vote_idx.lower_bound( business.account );

   flat_set< account_name_type > executive_account_list;
   flat_set< account_name_type > officer_account_list;

   while( bus_officer_vote_itr != bus_officer_vote_idx.end() &&
      bus_officer_vote_itr->business_account == business.account )
   {
      const account_object& voter = get_account( bus_officer_vote_itr->account );
      share_type weight = get_equity_voting_power( bus_officer_vote_itr->account, business );

      while( bus_officer_vote_itr != bus_officer_vote_idx.end() && 
         bus_officer_vote_itr->business_account == business.account &&
         bus_officer_vote_itr->account == voter.name )
      {
         const account_officer_vote_object& vote = *bus_officer_vote_itr;
         officers[ vote.officer_account ] += share_type( weight.value >> vote.vote_rank );
         // divides voting weight by 2^vote_rank, limiting total voting weight -> total voting power as votes increase.
         ++bus_officer_vote_itr;
      }
   }

   // Remove officers from map that do not meet voting requirement
   for( auto itr = officers.begin(); itr != officers.end(); )
   {
      if( itr->second < business.officer_vote_threshold )
      {
         itr = officers.erase( itr );   
      }
      else
      {
         officer_account_list.insert( itr->first );
         ++itr;
      }
   }

   flat_map< executive_role_type, share_type > role_map;
   auto bus_executive_vote_itr = bus_executive_vote_idx.lower_bound( business.account );

   while( bus_executive_vote_itr != bus_executive_vote_idx.end() && 
      bus_executive_vote_itr->business_account == business.account )
   {
      const account_object& voter = get_account( bus_executive_vote_itr->account );
      share_type weight = get_equity_voting_power( bus_executive_vote_itr->account, business );

      while( bus_executive_vote_itr != bus_executive_vote_idx.end() && 
         bus_executive_vote_itr->business_account == business.account &&
         bus_executive_vote_itr->account == voter.name )
      {
         const account_executive_vote_object& vote = *bus_executive_vote_itr;

         exec_map[ vote.executive_account ][ vote.role ] += share_type( weight.value >> vote.vote_rank );
         // divides voting weight by 2^vote_rank, limiting total voting weight -> total voting power as votes increase.
         ++bus_executive_vote_itr;
      }
   }
      
   for( auto exec_votes : exec_map )
   {
      for( auto role_votes : exec_votes.second )   // Copy all exec role votes into sorting vector
      {
         role_rank.push_back( std::make_pair( exec_votes.first, std::make_pair( role_votes.first, role_votes.second ) ) );
      }
   }
   
   std::sort( role_rank.begin(), role_rank.end(), [&]( pair< account_name_type, pair< executive_role_type, share_type > > a, pair< account_name_type, pair< executive_role_type, share_type > > b )
   {
      return a.second.second < b.second.second;   // Ordered vector of all executives, for each role. 
   });

   auto role_rank_itr = role_rank.begin();

   while( !exec_set.allocated() && 
      role_rank_itr != role_rank.end() )
   {
      pair< account_name_type, pair< executive_role_type, share_type > > rank = *role_rank_itr;
      
      account_name_type executive = rank.first;
      executive_role_type role = rank.second.first;
      share_type votes = rank.second.second;

      switch( role )
      {
         case executive_role_type::CHIEF_EXECUTIVE_OFFICER:
         {
            executives[executive] = std::make_pair( role, votes );
            exec_set.CHIEF_EXECUTIVE_OFFICER = executive;
         }
         break;
         case executive_role_type::CHIEF_OPERATING_OFFICER:
         {
            executives[executive] = std::make_pair( role, votes );
            exec_set.CHIEF_OPERATING_OFFICER = executive;
         }
         break;
         case executive_role_type::CHIEF_FINANCIAL_OFFICER:
         {
            executives[executive] = std::make_pair( role, votes );
            exec_set.CHIEF_FINANCIAL_OFFICER = executive;
         }
         break;
         case executive_role_type::CHIEF_TECHNOLOGY_OFFICER:
         {
            executives[executive] = std::make_pair( role, votes );
            exec_set.CHIEF_TECHNOLOGY_OFFICER = executive;
         }
         break;
         case executive_role_type::CHIEF_DEVELOPMENT_OFFICER:
         {
            executives[executive] = std::make_pair( role, votes );
            exec_set.CHIEF_DEVELOPMENT_OFFICER = executive;
         }
         break;
         case executive_role_type::CHIEF_SECURITY_OFFICER:
         {
            executives[executive] = std::make_pair( role, votes );
            exec_set.CHIEF_SECURITY_OFFICER = executive;
         }
         break;
         case executive_role_type::CHIEF_ADVOCACY_OFFICER:
         {
            executives[executive] = std::make_pair( role, votes );
            exec_set.CHIEF_ADVOCACY_OFFICER = executive;
         }
         break;
         case executive_role_type::CHIEF_GOVERNANCE_OFFICER:
         {
            executives[executive] = std::make_pair( role, votes );
            exec_set.CHIEF_GOVERNANCE_OFFICER = executive;
         }
         break;
         case executive_role_type::CHIEF_MARKETING_OFFICER:
         {
            executives[executive] = std::make_pair( role, votes );
            exec_set.CHIEF_MARKETING_OFFICER = executive;
         }
         break;
         case executive_role_type::CHIEF_DESIGN_OFFICER:
         {
            executives[executive] = std::make_pair( role, votes );
            exec_set.CHIEF_DESIGN_OFFICER = executive;
         }
         break;
      }

      executive_account_list.insert( executive );
      ++role_rank_itr;
   }

   modify( business, [&]( account_business_object& b )
   {
      b.officers = officer_account_list;
      b.executives = executive_account_list;
      b.officer_votes = officers;
      b.executive_votes = executives;
      b.executive_board = exec_set;
   });

   ilog( "Updated Business Account: ${b}",("b",business ) );

} FC_CAPTURE_AND_RETHROW() }


/**
 * Updates the executive board votes and positions of officers
 * in a business account.
 */
void database::update_business_account_set()
{ try {
   if( (head_block_num() % SET_UPDATE_BLOCK_INTERVAL) != 0 )    // Runs once per day
      return;

   const auto& business_idx = get_index< account_business_index >().indices().get< by_account >();
   auto business_itr = business_idx.begin();

   while( business_itr != business_idx.end() )
   {
      update_business_account( *business_itr );
      ++business_itr;
   }
} FC_CAPTURE_AND_RETHROW() }

/**
 * Process updates across all stablecoins, execute collateral bids
 * for settled stablecoins, and update price feeds and force settlement volumes
 */
void database::process_stablecoins()
{ try {
   if( (head_block_num() % STABLECOIN_BLOCK_INTERVAL) != 0 )    // Runs once per day
      return;

   // ilog( "Process Stablecoins" );

   time_point_sec now = head_block_time();
   uint64_t head_epoch_seconds = now.sec_since_epoch();

   const auto& stablecoin_idx = get_index< asset_stablecoin_data_index >().indices().get< by_symbol >();
   auto stablecoin_itr = stablecoin_idx.begin();

   while( stablecoin_itr != stablecoin_idx.end() )
   {
      const asset_stablecoin_data_object& stablecoin = *stablecoin_itr;
      ++stablecoin_itr;

      const asset_object& asset_obj = get_asset( stablecoin.symbol );
      uint32_t flags = asset_obj.flags;
      uint64_t feed_lifetime = stablecoin.feed_lifetime.to_seconds();

      if( stablecoin.has_settlement() )
      {
         process_bids( stablecoin );
      }

      modify( stablecoin, [&]( asset_stablecoin_data_object& abdo )
      {
         abdo.force_settled_volume = 0;        // Reset all BitAsset force settlement volumes to zero

         if ( ( flags & int( asset_issuer_permission_flags::producer_fed_asset ) ) &&
              feed_lifetime < head_epoch_seconds )            // if smartcoin && check overflow
         {
            fc::time_point calculated = now - feed_lifetime;

            for( auto feed_itr = abdo.feeds.rbegin(); feed_itr != abdo.feeds.rend(); )       // loop feeds
            {
               auto feed_time = feed_itr->second.first;
               std::advance( feed_itr, 1 );

               if( feed_time < calculated )
               {
                  abdo.feeds.erase( feed_itr.base() ); // delete expired feed
               }
            }
         }
      });
   }
} FC_CAPTURE_AND_RETHROW() }


/**
 * Allocates rewards to staked currency asset holders
 * according to proportional balances.
 */
void database::process_power_rewards()
{ try {
   if( (head_block_num() % EQUITY_INTERVAL_BLOCKS) != 0 )    // Runs once per week
      return;

   // ilog( "Process Power Rewards" );

   const auto& balance_idx = get_index< account_balance_index >().indices().get< by_symbol_stake >();
   const auto& fund_idx = get_index< reward_fund_index >().indices().get< by_symbol >();
   auto fund_itr = fund_idx.begin();

   while( fund_itr != fund_idx.end() )
   {
      const reward_fund_object& reward_fund = *fund_itr;

      asset power_reward_balance = reward_fund.power_reward_balance;       // Record the opening balance of the power reward fund
      auto balance_itr = balance_idx.lower_bound( reward_fund.symbol );
      flat_map < account_name_type, uint128_t > power_map;
      uint128_t total_power_shares = 0;
      asset distributed = asset( 0, reward_fund.symbol );

      while( balance_itr != balance_idx.end() && 
         balance_itr->symbol == reward_fund.symbol && 
         balance_itr->staked_balance >= BLOCKCHAIN_PRECISION )
      {
         uint128_t power_shares = balance_itr->staked_balance.value;         // Get the staked balance for each stakeholder.

         if( power_shares > uint128_t( 0 ) )
         {
            total_power_shares += power_shares;
            power_map[ balance_itr->owner ] = power_shares;
         }
         ++balance_itr;
      }

      for( auto b : power_map )
      {
         uint128_t reward_amount = ( uint128_t( power_reward_balance.amount.value ) * b.second ) / total_power_shares;
         asset power_reward = asset( reward_amount.to_uint64(), reward_fund.symbol );
         adjust_staked_balance( b.first, power_reward );       // Pay power reward to each stakeholder account proportionally.
         distributed += power_reward;
      }

      modify( reward_fund, [&]( reward_fund_object& r )
      {
         r.adjust_power_reward_balance( -distributed ); 
      });

      adjust_pending_supply( -distributed );   // Deduct distributed amount from pending supply.

      ++fund_itr;
   }
} FC_CAPTURE_AND_RETHROW() }


/**
 * Calaulates the relative share of equity reward dividend 
 * distribution that an account should receive
 * based on its balances, and account activity.
 */
share_type database::get_equity_shares( const account_balance_object& balance, const asset_equity_data_object& equity )
{
   const account_object& account = get_account( balance.owner );
   time_point now = head_block_time();
   if( ( account.producer_vote_count < equity.min_producers ) || 
      ( now > (account.last_activity_reward + equity.min_active_time ) ) )
   {
      return 0;  // Account does not receive equity reward when producer votes or last activity are insufficient.
   }

   share_type equity_shares = 0;
   equity_shares += ( equity.liquid_dividend_percent * balance.liquid_balance ) / PERCENT_100;
   equity_shares += ( equity.staked_dividend_percent * balance.staked_balance ) / PERCENT_100;
   equity_shares += ( equity.savings_dividend_percent * balance.savings_balance ) / PERCENT_100; 

   if( (balance.staked_balance >= equity.boost_balance ) &&
      (account.producer_vote_count >= equity.boost_producers ) &&
      (account.recent_activity_claims >= equity.boost_activity ) )
   {
      equity_shares *= 2;    // Doubles equity reward when 10+ WYM balance, 50+ producer votes, and 15+ Activity rewards in last 30 days
   }

   if( account.membership == membership_tier_type::TOP_MEMBERSHIP ) 
   {
      equity_shares = ( equity_shares * equity.boost_top ) / PERCENT_100;
   }

   return equity_shares;
}


/**
 * Allocates equity asset dividends from each dividend reward pool,
 * according to proportional balances.
 */
void database::process_equity_rewards()
{ try {
   if( (head_block_num() % EQUITY_INTERVAL_BLOCKS) != 0 )    // Runs once per week
      return;

   time_point now = head_block_time();
   const auto& equity_idx = get_index< asset_equity_data_index >().indices().get< by_symbol >();
   const auto& balance_idx = get_index< account_balance_index >().indices().get< by_symbol_stake >();
   auto equity_itr = equity_idx.begin();

   while( equity_itr != equity_idx.end() )
   {
      const asset_equity_data_object& equity = *equity_itr;

      for( auto a : equity.dividend_pool )     // Distribute every asset in the dividend pool
      {
         if( a.second.amount > 0 )
         {
            asset equity_reward_balance = a.second;  // Record the opening balance of the equity reward fund
            auto balance_itr = balance_idx.lower_bound( equity.symbol );
            flat_map < account_name_type, uint128_t > equity_map;
            uint128_t total_equity_shares = 0;
            asset distributed = asset( 0, a.first );

            while( balance_itr != balance_idx.end() &&
               balance_itr->symbol == equity.symbol ) 
            {
               share_type equity_shares = get_equity_shares( *balance_itr, equity );  // Get the equity shares for each stakeholder

               if( equity_shares > 0 )
               {
                  total_equity_shares += equity_shares.value;
                  equity_map[ balance_itr->owner ] = equity_shares.value;
               }
               ++balance_itr;
            }

            for( auto b : equity_map )
            {
               uint128_t reward_amount = ( uint128_t( equity_reward_balance.amount.value ) * b.second ) / total_equity_shares;
               asset equity_reward = asset( reward_amount.to_uint64(), equity_reward_balance.symbol ); 
               adjust_reward_balance( b.first, equity_reward );       // Pay equity dividend to each stakeholder account proportionally.
               distributed += equity_reward;
            }

            modify( equity, [&]( asset_equity_data_object& e )
            {
               e.adjust_pool( -distributed ); 
               e.last_dividend = now;        // Remove the distributed amount from the dividend pool.
            });

            adjust_pending_supply( -distributed );   // Deduct distributed amount from pending supply.
         }
      }
      
      ++equity_itr;
   }
} FC_CAPTURE_AND_RETHROW() }


/**
 * Updates the difficulty required for the network 
 * to track the targeted proof of work production rate.
 */
void database::update_proof_of_work_target()
{ try {
   if( (head_block_num() % POW_UPDATE_BLOCK_INTERVAL) != 0 )    // Runs once per day
      return;

   const median_chain_property_object& median_props = get_median_chain_properties();
   const producer_schedule_object& pso = get_producer_schedule();
   time_point now = head_block_time();

   modify( pso, [&]( producer_schedule_object& pso )
   {
      pso.decay_pow( now, median_props );
   });

   uint128_t recent_pow = pso.recent_pow;        // Amount of proofs of work, times block precision, decayed over 7 days
   x11 init = pso.pow_target_difficulty;

   if( recent_pow > 0 )
   {
      ilog( "=======================================================" );
      ilog( "========== Updating Proof of Work Difficulty ==========" );
      ilog( "=======================================================" );

      ilog( "   --> Recent POW:     ${r}",("r",recent_pow));
      uint128_t base = uint128_t::max_value();
      ilog( "   --> Base:           ${b}",("b",base));
      uint128_t dif = std::max( init.to_uint128(), uint128_t( 10 ));
      ilog( "   --> Dif:            ${b}",("b",dif));
      uint128_t coefficient = std::max( base / dif, uint128_t( 10 ));
      ilog( "   --> Coefficient:    ${b}",("b",coefficient));
      uint128_t target_pow = ( BLOCKCHAIN_PRECISION.value * median_props.pow_decay_time.to_seconds() ) / median_props.pow_target_time.to_seconds();
      ilog( "   --> Target POW:     ${b}",("b",target_pow));
      uint128_t mult = std::max( coefficient * recent_pow, target_pow );
      ilog( "   --> Mult:           ${b}",("b",mult));
      uint128_t div = std::max( mult / target_pow, uint128_t( 10 ));
      ilog( "   --> Div:            ${b}",("b",div));
      uint128_t target = base / div;
      ilog( "   --> Target:         ${b}",("b",target));
      x11 pow_target_difficulty = x11( target );
      ilog( "   --> Init:           ${i}",("i",init));
      ilog( "   --> Final:          ${d}",("d",pow_target_difficulty));

      modify( pso, [&]( producer_schedule_object& pso )
      {
         pso.pow_target_difficulty = pow_target_difficulty;
      });
   }
} FC_CAPTURE_AND_RETHROW() }

/**
 * Provides a producer account with a proof of work mining reward
 * and increments their mining power level for block production selection.
 * 
 * The top mining accounts are selected randomly once per round to 
 * produce a block at their scheduled time.
 */
void database::claim_proof_of_work_reward( const account_name_type& miner )
{ try {
   const median_chain_property_object& median_props = get_median_chain_properties();
   time_point now = head_block_time();
   const producer_schedule_object& pso = get_producer_schedule();
   const producer_object& producer = get_producer( miner );

   modify( producer, [&]( producer_object& p )
   {
      p.mining_power += BLOCKCHAIN_PRECISION;
      p.mining_count++;
      p.last_pow_time = now;
      p.decay_weights( now, median_props );
   });

   modify( pso, [&]( producer_schedule_object& pso )
   {
      pso.recent_pow += BLOCKCHAIN_PRECISION.value;
      pso.decay_pow( now, median_props );
   });

   const auto& fund_idx = get_index< reward_fund_index >().indices().get< by_symbol >();
   auto fund_itr = fund_idx.begin();

   while( fund_itr != fund_idx.end() )
   {
      const reward_fund_object& reward_fund = *fund_itr;
      asset pow_reward = reward_fund.work_reward_balance;

      modify( reward_fund, [&]( reward_fund_object& r )
      {
         r.adjust_work_reward_balance( -pow_reward );
      });

      adjust_reward_balance( miner, pow_reward );
      adjust_pending_supply( -pow_reward );

      ++fund_itr;
   }
} FC_CAPTURE_AND_RETHROW() }

/**
 * Distributes the transaction stake reward to all block producers according 
 * to the amount of stake weighted transactions included in blocks. 
 * Each transaction included in a block adds the size of the transaction 
 * multipled by the stake weight of its creator.
 */
void database::process_txn_stake_rewards()
{ try {
   if( (head_block_num() % TXN_STAKE_BLOCK_INTERVAL) != 0 )    // Runs once per Day
      return;

   // ilog( "Process Transaction Stake reward" );

   const auto& producer_idx = get_index< producer_index >().indices().get< by_txn_stake_weight >();
   auto producer_itr = producer_idx.begin();
    
   flat_map< account_name_type, uint128_t > stake_map;
   uint128_t total_stake_shares = 0;
   
   while( producer_itr != producer_idx.end() &&
      producer_itr->recent_txn_stake_weight > uint128_t( 0 ) )
   {
      uint128_t stake_shares = producer_itr->recent_txn_stake_weight;         // Get the recent txn stake for each producer

      if( stake_shares > uint128_t( 0 ) )
      {
         total_stake_shares += stake_shares;
         stake_map[ producer_itr->owner ] = stake_shares;
      }
      ++producer_itr;
   }

   const auto& fund_idx = get_index< reward_fund_index >().indices().get< by_symbol >();
   auto fund_itr = fund_idx.begin();

   while( fund_itr != fund_idx.end() && 
      total_stake_shares > uint128_t( 0 ) )
   {
      const reward_fund_object& reward_fund = *fund_itr;
      asset txn_stake_reward = reward_fund.txn_stake_reward_balance;     // Record the opening balance of the transaction stake reward fund
      asset distributed = asset( 0, reward_fund.symbol );

      for( auto b : stake_map )
      {
         uint128_t r_shares = ( uint128_t( txn_stake_reward.amount.value ) * b.second ) / total_stake_shares; 
         asset stake_reward = asset( share_type( int64_t( r_shares.to_uint64() ) ), reward_fund.symbol );
         adjust_reward_balance( b.first, stake_reward );       // Pay transaction stake reward to each block producer proportionally.
         distributed += stake_reward;
   
         // ilog( "Processing Txn Stake Reward for account: ${a} Reward: ${r}", ("a",b.first)("r",stake_reward));
      }

      modify( reward_fund, [&]( reward_fund_object& r )
      {
         r.adjust_txn_stake_reward_balance( -distributed );     // Remove the distributed amount from the reward pool.
      });

      adjust_pending_supply( -distributed );   // Deduct distributed amount from pending supply.

      ++fund_itr;
   }
} FC_CAPTURE_AND_RETHROW() }


/**
 * Distributes the block reward for validating blocks to producers
 * and miners according to the stake weight of their commitment transactions
 * upon the block becoming irreversible after majority of producers have created
 * a block on top of it.
 * 
 * This enables nodes to have a lower finality time in
 * cases where producers a majority of producers commit to a newly 
 * created block before it becomes irreversible.
 * Nodes will treat the blocks that they commit to as irreversible when
 * greater than two third of producers also commit to the same block.
 */
void database::process_validation_rewards()
{ try {
   // ilog( "Process Validation rewards" );

   const dynamic_global_property_object& props = get_dynamic_global_properties();
   const auto& valid_idx = get_index< block_validation_index >().indices().get< by_height_stake >();
   auto valid_itr = valid_idx.lower_bound( props.last_irreversible_block_num );
    
   flat_map < account_name_type, share_type > validation_map;
   share_type total_validation_shares = 0;

   while( valid_itr != valid_idx.end() &&
      valid_itr->block_height == props.last_irreversible_block_num &&
      valid_itr->commitment_stake.amount >= BLOCKCHAIN_PRECISION ) 
   {
      share_type validation_shares = valid_itr->commitment_stake.amount;       // Get the commitment stake for each producer

      if( validation_shares > 0 )
      {
         total_validation_shares += validation_shares;
         validation_map[ valid_itr->producer ] = validation_shares;
      }
      ++valid_itr;
   }

   const auto& fund_idx = get_index< reward_fund_index >().indices().get< by_symbol >();
   auto fund_itr = fund_idx.begin();

   while( fund_itr != fund_idx.end() && total_validation_shares > 0 )
   {
      const reward_fund_object& reward_fund = *fund_itr;
      asset validation_reward = reward_fund.validation_reward_balance;     // Record the opening balance of the validation reward fund
      asset distributed = asset( 0, reward_fund.symbol );

      for( auto b : validation_map )
      {
         asset validation_reward_split = ( validation_reward * b.second ) / total_validation_shares; 
         adjust_reward_balance( b.first, validation_reward );       // Pay transaction validation reward to each block producer proportionally.
         distributed += validation_reward_split;

         ilog( "Processing Validation Reward for account: ${a} \n ${r} \n",
            ("a",b.first)("r",validation_reward_split ) );
      }

      modify( reward_fund, [&]( reward_fund_object& r )
      {
         r.adjust_validation_reward_balance( -distributed );     // Remove the distributed amount from the reward pool.
      });

      adjust_pending_supply( -distributed );   // Deduct distributed amount from pending supply.

      ++fund_itr;
   }
} FC_CAPTURE_AND_RETHROW() }


/** 
 * Rewards producers when they have the current highest accumulated 
 * activity stake. Each time an account produces an activity reward transaction, 
 * they implicitly nominate their highest voted producer to receive a daily vote as their Prime Producer.
 * Award is distributed every eight hours to the leader by activity stake.
 * This incentivizes producers to campaign to achieve prime producer designation with 
 * high stake, active accounts, in a competitive manner. 
 */
void database::process_producer_activity_rewards()
{ try {
   if( (head_block_num() % POA_BLOCK_INTERVAL ) != 0 )    // Runs once per 8 hours.
      return;

   // ilog( "Process Producer Activity Rewards" );
   
   const auto& producer_idx = get_index< producer_index >().indices().get< by_activity_stake >();
   auto producer_itr = producer_idx.begin();

   const auto& fund_idx = get_index< reward_fund_index >().indices().get< by_symbol >();
   auto fund_itr = fund_idx.begin();

   if( producer_itr != producer_idx.end() )    // Get Top producer by activity stake
   {
      const producer_object& prime_producer = *producer_itr;

      modify( prime_producer, [&]( producer_object& p )
      {
         p.accumulated_activity_stake = 0;     // Reset activity stake for top producer.
      });

      while( fund_itr != fund_idx.end() )
      {
         const reward_fund_object& reward_fund = *fund_itr;
         asset poa_reward = reward_fund.producer_activity_reward_balance;    // Record the opening balance of the producer activity reward fund.

         modify( reward_fund, [&]( reward_fund_object& r )
         {
            r.adjust_producer_activity_reward_balance( -poa_reward );        // Remove the distributed amount from the reward pool.
         });

         adjust_reward_balance( prime_producer.owner, poa_reward );          // Pay producer activity reward to the producer with the highest accumulated activity stake.
         adjust_pending_supply( -poa_reward );                               // Deduct distributed amount from pending supply.

         ++fund_itr;
      }
   }
} FC_CAPTURE_AND_RETHROW() }


/**
 * Distributes Supernode rewards between all supernodes according to
 * stake weighted views on posts.
 */
void database::process_supernode_rewards()
{ try {
   if( (head_block_num() % SUPERNODE_BLOCK_INTERVAL ) != 0 )    // Runs once per day.
      return;

   // ilog( "Process Supernode Rewards" );

   time_point now = head_block_time();
   const auto& supernode_idx = get_index< supernode_index >().indices().get< by_view_weight >();
   const auto& sn_acc_idx = get_index< supernode_index >().indices().get< by_account >();
   auto supernode_itr = supernode_idx.begin();
   flat_map < account_name_type, share_type > supernode_map;
   share_type total_supernode_shares = 0;
   
   while( supernode_itr != supernode_idx.end() ) 
   {
      share_type supernode_shares = supernode_itr->recent_view_weight;  // Get the supernode view weight for rewards

      if( supernode_shares > 0 && supernode_itr->active && now > ( supernode_itr->last_activation_time + fc::days(1) ) )
      {
         total_supernode_shares += supernode_shares;
         supernode_map[ supernode_itr->account ] = supernode_shares;
      }
      ++supernode_itr;
   }

   const auto& fund_idx = get_index< reward_fund_index >().indices().get< by_symbol >();
   auto fund_itr = fund_idx.begin();

   while( fund_itr != fund_idx.end() && total_supernode_shares > 0 )
   {
      const reward_fund_object& reward_fund = *fund_itr;
      asset supernode_reward = reward_fund.supernode_reward_balance;     // Record the opening balance of the supernode reward fund
      asset distributed = asset( 0, reward_fund.symbol );

      for( auto b : supernode_map )
      {
         asset supernode_reward_split = ( supernode_reward * b.second ) / total_supernode_shares; 
         adjust_reward_balance( b.first, supernode_reward_split );       // Pay supernode reward proportionally with view weight.
         auto sn_ptr = sn_acc_idx.find( b.first );
         if( sn_ptr != sn_acc_idx.end() )
         {
            modify( *sn_ptr, [&]( supernode_object& s )
            {
               s.storage_rewards += supernode_reward_split;     // Increment the lifetime storage earnings of the supernode
            }); 
         }
         distributed += supernode_reward_split;
      }

      modify( reward_fund, [&]( reward_fund_object& r )
      {
         r.adjust_supernode_reward_balance( -distributed );     // Remove the distributed amount from the reward pool.
      });

      adjust_pending_supply( -distributed );               // Deduct distributed amount from pending supply.

      ++fund_itr;
   }
} FC_CAPTURE_AND_RETHROW() }


/**
 * Update a network officer's voting approval statisitics
 * and updates its approval if there are
 * sufficient votes from producers and other accounts.
 */
void database::update_network_officer( const network_officer_object& network_officer, 
   const producer_schedule_object& pso, const dynamic_global_property_object& props )
{ try {
   uint32_t vote_count = 0;
   share_type voting_power = 0;
   uint32_t producer_vote_count = 0;
   share_type producer_voting_power = 0;
   price equity_price = get_liquidity_pool( SYMBOL_COIN, SYMBOL_EQUITY ).hour_median_price;

   const auto& vote_idx = get_index< network_officer_vote_index >().indices().get< by_officer_account >();
   auto vote_itr = vote_idx.lower_bound( network_officer.account );

   while( vote_itr != vote_idx.end() && 
      vote_itr->network_officer == network_officer.account )
   {
      const network_officer_vote_object& vote = *vote_itr;
      const account_object& voter = get_account( vote.account );
      bool is_producer = pso.is_top_voting_producer( voter.name );
      vote_count++;
      share_type weight = 0;
      weight += get_voting_power( vote.account, equity_price );

      if( voter.proxied.size() )
      {
         weight += get_proxied_voting_power( voter, equity_price );
      }

      voting_power += share_type( weight.value >> vote.vote_rank );

      if( is_producer )
      {
         producer_vote_count++;
         const producer_object& producer = get_producer( voter.name );
         producer_voting_power += share_type( producer.voting_power.value >> vote.vote_rank );
      }
      ++vote_itr;
   }

   // Approve the network officer when a threshold of voting power and vote amount supports it.
   bool approve_officer = ( vote_count >= OFFICER_VOTE_THRESHOLD_AMOUNT ) &&
      ( producer_vote_count >= OFFICER_VOTE_THRESHOLD_PRODUCERS ) &&
      ( voting_power.value >= ( props.total_voting_power * OFFICER_VOTE_THRESHOLD_PERCENT ) / PERCENT_100 ) &&
      ( producer_voting_power.value >= ( pso.total_producer_voting_power * OFFICER_VOTE_THRESHOLD_PERCENT ) / PERCENT_100 );
   
   modify( network_officer, [&]( network_officer_object& n )
   {
      n.vote_count = vote_count;
      n.voting_power = voting_power;
      n.producer_vote_count = producer_vote_count;
      n.producer_voting_power = producer_voting_power;
      n.officer_approved = approve_officer;
   });

   ilog( "Updated Network Officer: ${n} Vote count: ${c} Approved: ${a}",
      ("n",network_officer.account)("c",vote_count)("a",approve_officer) );
   
} FC_CAPTURE_AND_RETHROW() }


/**
 * Pays the network officer rewards to the 50 highest voted
 * developers, marketers and advocates on the network from
 * all currency asset reward funds once per day.
 */
void database::process_network_officer_rewards()
{ try {
   if( (head_block_num() % NETWORK_OFFICER_BLOCK_INTERVAL ) != 0 )    // Runs once per day.
      return;

   // ilog( "Process Network Officer Rewards" );

   const dynamic_global_property_object& props = get_dynamic_global_properties();
   const producer_schedule_object& pso = get_producer_schedule();
   const auto& officer_idx = get_index< network_officer_index >().indices().get< by_type_voting_power >();
   auto officer_itr = officer_idx.begin();

   while( officer_itr != officer_idx.end() ) 
   {
      update_network_officer( *officer_itr, pso, props );
      ++officer_itr;
   }

   // ========== Development Officers ========== //

   auto development_itr = officer_idx.lower_bound( network_officer_role_type::DEVELOPMENT );
   auto development_end = officer_idx.upper_bound( network_officer_role_type::DEVELOPMENT );
   flat_map < account_name_type, share_type > development_map;
   share_type total_development_shares = 0;

   while( development_itr != development_end && 
      development_map.size() < NETWORK_OFFICER_ACTIVE_SET ) 
   {
      share_type development_shares = development_itr->voting_power;  // Get the development officer voting power

      if( development_shares > 0 && development_itr->active && development_itr->officer_approved )
      {
         total_development_shares += development_shares;
         development_map[ development_itr->account ] = development_shares;
      }
      ++development_itr;
   }

   // ========== Marketing Officers ========== //

   auto marketing_itr = officer_idx.lower_bound( network_officer_role_type::MARKETING );
   auto marketing_end = officer_idx.upper_bound( network_officer_role_type::MARKETING );
   flat_map < account_name_type, share_type > marketing_map;
   share_type total_marketing_shares = 0;
   
   while( marketing_itr != marketing_end && 
      marketing_map.size() < NETWORK_OFFICER_ACTIVE_SET ) 
   {
      share_type marketing_shares = marketing_itr->voting_power;  // Get the marketing officer voting power

      if( marketing_shares > 0 && marketing_itr->active && marketing_itr->officer_approved )
      {
         total_marketing_shares += marketing_shares;
         marketing_map[ marketing_itr->account ] = marketing_shares;
      }
      ++marketing_itr;
   }

   // ========== Advocacy Officers ========== //

   auto advocacy_itr = officer_idx.lower_bound( network_officer_role_type::ADVOCACY );
   auto advocacy_end = officer_idx.upper_bound( network_officer_role_type::ADVOCACY );
   flat_map < account_name_type, share_type > advocacy_map;
   share_type total_advocacy_shares = 0;
   
   while( advocacy_itr != advocacy_end && advocacy_map.size() < NETWORK_OFFICER_ACTIVE_SET ) 
   {
      share_type advocacy_shares = advocacy_itr->voting_power;  // Get the advocacy officer voting power

      if( advocacy_shares > 0 && advocacy_itr->active && advocacy_itr->officer_approved )
      {
         total_advocacy_shares += advocacy_shares;
         advocacy_map[ advocacy_itr->account ] = advocacy_shares;
      }
      ++advocacy_itr;
   }

   const auto& fund_idx = get_index< reward_fund_index >().indices().get< by_symbol >();
   auto fund_itr = fund_idx.begin();

   while( fund_itr != fund_idx.end() )
   {
      const reward_fund_object& reward_fund = *fund_itr;
      asset development_reward = reward_fund.development_reward_balance;
      asset development_distributed = asset( 0, reward_fund.symbol );
      asset marketing_reward = reward_fund.marketing_reward_balance;
      asset marketing_distributed = asset( 0, reward_fund.symbol );
      asset advocacy_reward = reward_fund.advocacy_reward_balance;
      asset advocacy_distributed = asset( 0, reward_fund.symbol );

      for( auto b : development_map )
      {
         asset development_reward_split = ( development_reward * b.second ) / total_development_shares;
         adjust_reward_balance( b.first, development_reward_split );
         development_distributed += development_reward_split;
      }

      for( auto b : marketing_map )
      {
         asset marketing_reward_split = ( marketing_reward * b.second ) / total_marketing_shares;
         adjust_reward_balance( b.first, marketing_reward_split );
         marketing_distributed += marketing_reward_split;
      }

      for( auto b : advocacy_map )
      {
         asset advocacy_reward_split = ( advocacy_reward * b.second ) / total_advocacy_shares;
         adjust_reward_balance( b.first, advocacy_reward_split );
         advocacy_distributed += advocacy_reward_split;
      }

      modify( reward_fund, [&]( reward_fund_object& r )
      {
         r.adjust_development_reward_balance( -development_distributed );   
         r.adjust_marketing_reward_balance( -marketing_distributed );   
         r.adjust_advocacy_reward_balance( -advocacy_distributed );  
      });

      asset total_distributed = development_distributed + marketing_distributed + advocacy_distributed;
      adjust_pending_supply( -total_distributed );   // Deduct distributed amount from pending supply.

      ++fund_itr;
   }
} FC_CAPTURE_AND_RETHROW() }


/**
 * Update an executive board's voting approval statisitics
 * and update its approval if there are
 * sufficient votes from producers and other accounts.
 */
void database::update_executive_board( const executive_board_object& executive_board, 
   const producer_schedule_object& pso, const dynamic_global_property_object& props )
{ try {
   uint32_t vote_count = 0;
   share_type voting_power = 0;
   uint32_t producer_vote_count = 0;
   share_type producer_voting_power = 0;
   price equity_price = get_liquidity_pool(SYMBOL_COIN, SYMBOL_EQUITY).hour_median_price;

   const auto& vote_idx = get_index< executive_board_vote_index >().indices().get< by_executive_account >();
   auto vote_itr = vote_idx.lower_bound( executive_board.account );

   while( vote_itr != vote_idx.end() && 
      vote_itr->executive_board == executive_board.account )
   {
      const executive_board_vote_object& vote = *vote_itr;
      const account_object& voter = get_account( vote.account );
      bool is_producer = pso.is_top_voting_producer( voter.name );
      vote_count++;
      share_type weight = 0;
      weight += get_voting_power( vote.account, equity_price );
      if( voter.proxied.size() )
      {
         weight += get_proxied_voting_power( voter, equity_price );
      }
      voting_power += share_type( weight.value >> vote.vote_rank );

      if( is_producer )
      {
         producer_vote_count++;
         const producer_object& producer = get_producer( voter.name );
         producer_voting_power += share_type( producer.voting_power.value >> vote.vote_rank );
      }
      ++vote_itr;
   }

   // Approve the executive board when a threshold of accounts vote to support its budget.
   bool approve_board = ( vote_count >= EXECUTIVE_VOTE_THRESHOLD_AMOUNT ) &&
      ( producer_vote_count >= EXECUTIVE_VOTE_THRESHOLD_PRODUCERS ) &&
      ( voting_power.value >= ( props.total_voting_power * EXECUTIVE_VOTE_THRESHOLD_PERCENT ) / PERCENT_100 ) &&
      ( producer_voting_power.value >= ( pso.total_producer_voting_power * EXECUTIVE_VOTE_THRESHOLD_PERCENT ) / PERCENT_100 );
   
   modify( executive_board, [&]( executive_board_object& e )
   {
      e.vote_count = vote_count;
      e.voting_power = voting_power;
      e.producer_vote_count = producer_vote_count;
      e.producer_voting_power = producer_voting_power;
      e.board_approved = approve_board;
   });

   ilog( "Update Executive Board: ${b} Vote count: ${v} Approved: ${a}", 
      ("b",executive_board.account)("v",vote_count)("a",approve_board));

} FC_CAPTURE_AND_RETHROW() }


/**
 * Pays the requested budgets of the approved executive boards on the network.
 * 
 * Boards that have sufficient approval from accounts and producers paid once per day.
 * Price of network credit asset must be greater than $0.90 USD to issue new units, or 
 * executive budgets are suspended. 
 * Network credit is a credit currency that is issued to executive boards
 * for expenses of managing a network development team. Its value is derived from
 * buybacks from network revenue, up to a face value of $1.00 USD
 * per credit, and interest payments for balance holders.
 * Holding Credit assets are economically equivalent to holding bonds
 * for debt lent to the network. 
 */
void database::process_executive_board_budgets()
{ try {
   if( (head_block_num() % EXECUTIVE_BOARD_BLOCK_INTERVAL ) != 0 )    // Runs once per day.
      return;

   // ilog( "Process Executive Board Budgets" );

   const producer_schedule_object& pso = get_producer_schedule();
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   price credit_usd_price = get_liquidity_pool( SYMBOL_USD, SYMBOL_CREDIT ).hour_median_price;

   const auto& exec_idx = get_index< executive_board_index >().indices().get< by_voting_power >();
   auto exec_itr = exec_idx.begin();

   while( exec_itr != exec_idx.end() )   // update all executive board approvals and vote statistics. 
   {
      const executive_board_object& exec = *exec_itr;
      update_executive_board( exec, pso, props );
      ++exec_itr;
   }

   if( credit_usd_price > MIN_EXEC_CREDIT_PRICE )
   {
      auto exec_itr = exec_idx.begin(); // reset iterator;

      while( exec_itr != exec_idx.end() )   // Pay the budget requests of the approved executive boards.
      {
         const executive_board_object& exec = *exec_itr;

         if( exec.board_approved )
         {
            ilog( "Processed Executive Board Budget: ${a} \n ${b} \n", 
               ("b",exec) );
            adjust_liquid_balance( exec.account, exec.budget );     // Issues new supply of credit asset to pay executive board.
         }
         ++exec_itr;
      }
   }
} FC_CAPTURE_AND_RETHROW() }


/**
 * Update a governance account's voting approval statisitics
 * and update its approval if there are
 * sufficient votes from producers and other accounts.
 */
void database::update_governance_account( const governance_account_object& governance_account, 
   const producer_schedule_object& pso, const dynamic_global_property_object& props )
{ try {
   uint32_t vote_count = 0;
   share_type voting_power = 0;
   uint32_t producer_vote_count = 0;
   share_type producer_voting_power = 0;
   price equity_price = get_liquidity_pool( SYMBOL_COIN, SYMBOL_EQUITY ).hour_median_price;

   const auto& vote_idx = get_index< governance_subscription_index >().indices().get< by_governance_account >();
   auto vote_itr = vote_idx.lower_bound( governance_account.account );

   while( vote_itr != vote_idx.end() && 
      vote_itr->governance_account == governance_account.account )
   {
      const governance_subscription_object& vote = *vote_itr;
      const account_object& voter = get_account( vote.account );
      bool is_producer = pso.is_top_voting_producer( voter.name );
      vote_count++;
      share_type weight = 0;
      weight += get_voting_power( vote.account, equity_price );
      if( voter.proxied.size() )
      {
         weight += get_proxied_voting_power( voter, equity_price );
      }
      voting_power += share_type( weight.value >> vote.vote_rank );

      if( is_producer )
      {
         producer_vote_count++;
         const producer_object& producer = get_producer( voter.name );
         producer_voting_power += share_type( producer.voting_power.value >> vote.vote_rank );
      }
      ++vote_itr;
   }

   // Approve the governance account when a threshold of votes to support its budget.
   bool approve_account = ( vote_count >= GOVERNANCE_VOTE_THRESHOLD_AMOUNT ) &&
      ( producer_vote_count >= GOVERNANCE_VOTE_THRESHOLD_PRODUCERS ) &&
      ( voting_power.value >= ( props.total_voting_power * GOVERNANCE_VOTE_THRESHOLD_PERCENT ) / PERCENT_100 ) &&
      ( producer_voting_power.value >= ( pso.total_producer_voting_power * GOVERNANCE_VOTE_THRESHOLD_PERCENT ) / PERCENT_100 );
   
   modify( governance_account, [&]( governance_account_object& g )
   {
      g.subscriber_count = vote_count;
      g.subscriber_power = voting_power;
      g.producer_subscriber_count = producer_vote_count;
      g.producer_subscriber_power = producer_voting_power;
      g.account_approved = approve_account;
   });

   ilog( "Update Governance Account: ${g} Subscribers: ${s} Approved: ${a}",
      ("g",governance_account.account)("s",vote_count)("a",approve_account));

} FC_CAPTURE_AND_RETHROW() }


void database::update_governance_account_set()
{ try { 
   if( (head_block_num() % SET_UPDATE_BLOCK_INTERVAL ) != 0 )    // Runs once per day
      return;

   // ilog( "Update Governance Account Set" );
   
   const producer_schedule_object& pso = get_producer_schedule();
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   const auto& g_idx = get_index< governance_account_index >().indices().get< by_subscriber_power >();
   auto g_itr = g_idx.begin();
   
   while( g_itr != g_idx.end() )
   {
      update_governance_account( *g_itr, pso, props );
      ++g_itr;
   }

} FC_CAPTURE_AND_RETHROW() }


/**
 * Update a community enterprise proposal's voting approval statisitics
 * and increment the approved milestone if there are
 * sufficient current approvals from producers and other accounts.
 */
void database::update_enterprise( const community_enterprise_object& enterprise, 
   const producer_schedule_object& pso, const dynamic_global_property_object& props )
{ try {
   uint32_t total_approvals = 0;
   share_type total_voting_power = 0;
   uint32_t total_producer_approvals = 0;
   share_type total_producer_voting_power = 0;
   uint32_t current_approvals = 0;
   share_type current_voting_power = 0;
   uint32_t current_producer_approvals = 0;
   share_type current_producer_voting_power = 0;
   price equity_price = get_liquidity_pool( SYMBOL_COIN, SYMBOL_EQUITY ).hour_median_price;

   const auto& approval_idx = get_index< enterprise_approval_index >().indices().get< by_enterprise_id >();
   auto approval_itr = approval_idx.lower_bound( boost::make_tuple( enterprise.creator, enterprise.enterprise_id ) );

   while( approval_itr != approval_idx.end() && 
      approval_itr->creator == enterprise.creator && 
      approval_itr->enterprise_id == enterprise.enterprise_id )
   {
      const enterprise_approval_object& approval = *approval_itr;
      const account_object& voter = get_account( approval.account );

      bool is_producer = pso.is_top_voting_producer( voter.name );
      total_approvals++;
      total_voting_power += get_voting_power( approval.account, equity_price );

      if( voter.proxied.size() )
      {
         total_voting_power += get_proxied_voting_power( voter, equity_price );
      }

      if( is_producer )
      {
         total_producer_approvals++;
         const producer_object& producer = get_producer( voter.name );
         total_producer_voting_power += producer.voting_power.value;
      }

      if( approval.milestone >= enterprise.claimed_milestones )
      {
         current_approvals++;
         current_voting_power += get_voting_power( approval.account, equity_price );
         if( voter.proxied.size() )
         {
            current_voting_power += get_proxied_voting_power( voter, equity_price );
         }

         if( is_producer )
         {
            current_producer_approvals++;
            const producer_object& producer = get_producer( voter.name );
            current_producer_voting_power += producer.voting_power.value;
         }
      }
      ++approval_itr;
   }

   // Approve the latest claimed milestone when a threshold of approvals support its release.

   bool approve_milestone = ( current_approvals >= ENTERPRISE_VOTE_THRESHOLD_AMOUNT ) &&
      ( current_producer_approvals >= ENTERPRISE_VOTE_THRESHOLD_PRODUCERS ) &&
      ( current_voting_power.value >= ( props.total_voting_power * ENTERPRISE_VOTE_THRESHOLD_PERCENT ) / PERCENT_100 ) &&
      ( current_producer_voting_power.value >= ( pso.total_producer_voting_power * ENTERPRISE_VOTE_THRESHOLD_PERCENT ) / PERCENT_100 );

   modify( enterprise, [&]( community_enterprise_object& e )
   {
      e.total_approvals = total_approvals;
      e.total_voting_power = total_voting_power;
      e.total_producer_approvals = total_producer_approvals;
      e.total_producer_voting_power = total_producer_voting_power;
      e.current_approvals = current_approvals;
      e.current_voting_power = current_voting_power;
      e.current_producer_approvals = current_producer_approvals;
      e.current_producer_voting_power = current_producer_voting_power;

      if( approve_milestone )
      {
         e.approved_milestones = e.claimed_milestones;
      }
   });

   ilog( "Updated Enterprise: ${e} - Total Approvals: ${t} - Current Approvals: ${a} - Claimed Milestones: ${m} - Days Paid: ${dp} - Approved Milestones: ${ap}",
      ("e", enterprise.enterprise_id )("t",enterprise.total_approvals)("a",enterprise.current_approvals)
      ("m",enterprise.claimed_milestones)("dp",enterprise.days_paid)("ap",enterprise.approved_milestones));

} FC_CAPTURE_AND_RETHROW() }


/**
 * Updates all community enterprise proposals, by checking 
 * if they have sufficient approvals from accounts on 
 * the network and producers.
 * Processes budget payments for all proposals that have milestone approvals.
 */
void database::process_community_enterprise_fund()
{ try {
   if( (head_block_num() % ENTERPRISE_BLOCK_INTERVAL ) != 0 )    // Runs once per day.
      return;

   const producer_schedule_object& pso = get_producer_schedule();
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   time_point now = head_block_time();
   const auto& enterprise_idx = get_index< community_enterprise_index >().indices().get< by_total_voting_power >();
   auto enterprise_itr = enterprise_idx.begin();

   while( enterprise_itr != enterprise_idx.end() )
   {
      update_enterprise( *enterprise_itr, pso, props );
      ++enterprise_itr;
   }

   enterprise_itr = enterprise_idx.begin();

   while( enterprise_itr != enterprise_idx.end() )   // Enterprise objects in order of highest total voting power.
   {
      const reward_fund_object& reward_fund = get_reward_fund( enterprise_itr->daily_budget.symbol );

      if( enterprise_itr->approved_milestones >= 0 && 
         enterprise_itr->begin > now && 
         reward_fund.community_fund_balance.amount > 0 )        // Processed when they have inital approval and passed begin time.
      {
         const community_enterprise_object& enterprise = *enterprise_itr;
         asset available_budget = std::min( reward_fund.community_fund_balance, enterprise.daily_budget );

         if( enterprise.duration > enterprise.days_paid )
         {
            modify( reward_fund, [&]( reward_fund_object& r )
            {
               r.adjust_community_fund_balance( -available_budget );     // Remove the distributed amount from the reward pool.
            });

            modify( enterprise, [&]( community_enterprise_object& e )
            {
               e.adjust_pending_budget( available_budget );     // Pay daily budget to enterprise proposal
               e.days_paid++;
            });

            ilog( "Processed Community Enterprise Budget - ID: ${id} - Days Paid: ${dp} - Budget Paid: ${b}",
               ("id",enterprise.enterprise_id )("dp",enterprise.days_paid)("b",available_budget));
         }

         uint16_t percent_released = 0;

         for( auto i = 0; i <= enterprise.approved_milestones; i++ )
         {
            percent_released += enterprise.milestone_shares[ i ];  // Accumulate all approved milestone percentages
         }

         asset release_limit = ( enterprise.total_budget() * percent_released ) / PERCENT_100;
         asset to_release = std::min( enterprise.pending_budget, release_limit - enterprise.total_distributed );
         
         if( to_release.amount > 0 )
         {
            asset distributed = asset( 0, SYMBOL_COIN );

            for( auto b : enterprise.beneficiaries )
            {
               asset release_split = ( to_release * b.second ) / PERCENT_100; 
               adjust_liquid_balance( b.first, release_split );       // Pay proposal beneficiaries according to percentage split.
               distributed += release_split;
            }

            adjust_pending_supply( -distributed );   // Deduct distributed amount from pending supply.

            modify( enterprise, [&]( community_enterprise_object& e )
            {
               e.adjust_pending_budget( -distributed );
               e.total_distributed += distributed;
            });
         }
         ilog( "Process Enterprise Funding ${e} - Total Approvals: ${t} - Current Approvals: ${a} - Claimed Milestones: ${m} - Days Paid: ${dp} - Approved Milestones: ${ap}",
            ("e",enterprise.enterprise_id )("t",enterprise.total_approvals)("a",enterprise.current_approvals)
            ("m",enterprise.claimed_milestones)("dp",enterprise.days_paid)("ap",enterprise.approved_milestones));
      }
      ++enterprise_itr;
   }
} FC_CAPTURE_AND_RETHROW() }


/** 
 * Updates the state of all credit loans.
 * 
 * Compounds interest on all credit loans, 
 * checks collateralization ratios, and 
 * liquidates under collateralized loans in 
 * response to price changes.
 */
void database::process_credit_updates()
{ try {
   const median_chain_property_object& median_props = get_median_chain_properties();
   time_point now = head_block_time();

   const auto& loan_idx = get_index< credit_loan_index >().indices().get< by_liquidation_spread >();
   auto loan_itr = loan_idx.begin();

   // ilog( "Process Credit Updates" );

   while( loan_itr != loan_idx.end() )
   {
      const asset_object& debt_asset = get_asset( loan_itr->debt_asset() );
      const asset_credit_pool_object& credit_pool = get_credit_pool( loan_itr->debt_asset(), false );
      uint16_t fixed = median_props.credit_min_interest;
      uint16_t variable = median_props.credit_variable_interest;
      share_type interest_rate = credit_pool.interest_rate( fixed, variable );
      asset total_interest = asset( 0, debt_asset.symbol );

      while( loan_itr != loan_idx.end() && 
         loan_itr->debt_asset() == debt_asset.symbol )
      {
         const asset_object& collateral_asset = get_asset( loan_itr->collateral_asset() );
         const asset_liquidity_pool_object& pool = get_liquidity_pool( loan_itr->symbol_liquid );
         price col_debt_price = pool.base_hour_median_price( loan_itr->collateral_asset() );

         while( loan_itr != loan_idx.end() &&
            loan_itr->debt_asset() == debt_asset.symbol &&
            loan_itr->collateral_asset() == collateral_asset.symbol )
         {
            const credit_loan_object& loan = *loan_itr;
            ++loan_itr;

            int64_t interest_seconds = ( now - loan.last_interest_time ).to_seconds();
            if( interest_seconds >= INTEREST_MIN_INTERVAL.to_seconds() )    // Check once every 60 seconds
            {
               uint128_t interest_amount = uint128_t( loan.debt.amount.value ) * uint128_t( interest_rate.value ) * uint128_t( interest_seconds );
               interest_amount /= uint128_t( fc::days(365).to_seconds() * PERCENT_100 );

               asset interest = asset( interest_amount.to_uint64(), debt_asset.symbol );
               asset max_debt = ( ( loan.collateral * col_debt_price ) * median_props.credit_liquidation_ratio ) / PERCENT_100;
               price liquidation_price = price( loan.collateral, max_debt );

               modify( loan, [&]( credit_loan_object& c )
               {
                  if( interest_amount > uint128_t( INTEREST_MIN_AMOUNT ) )
                  {
                     c.debt += interest;
                     c.interest += interest;
                     c.last_interest_rate = interest_rate;
                     c.last_interest_time = now;
                  }
                  c.liquidation_price = liquidation_price;
               });

               if( interest_amount > uint128_t( INTEREST_MIN_AMOUNT ) )    // Ensure interest is above dust to prevent lossy rounding
               {
                  total_interest += interest;
               }

               ilog( "Credit Loan Interest paid: ${i} Total Interest: ${t} Last Interest Rate: ${r}",
                  ("i",interest.to_string())("t",loan.interest.to_string())("r",fc::to_string( loan.real_interest_rate() ).substr(0,5) + "%") );

               if( loan.loan_price() < loan.liquidation_price )    // If loan falls below liquidation price
               {
                  liquidate_credit_loan( loan );                   // Liquidate it at current price
               }
            }
         }
      }

      modify( credit_pool, [&]( asset_credit_pool_object& c )
      {
         c.last_interest_rate = interest_rate;
         c.borrowed_balance += total_interest;
      });
   }
} FC_CAPTURE_AND_RETHROW() }


void database::adjust_view_weight( const supernode_object& supernode, share_type delta, bool adjust = true )
{ try {
   const median_chain_property_object& median_props = get_median_chain_properties();
   time_point now = head_block_time();

   modify( supernode, [&]( supernode_object& s )
   {
      s.decay_weights( median_props, now );
      s.recent_view_weight += delta;

      if( adjust )
      {
         s.daily_active_users += PERCENT_100;
         s.monthly_active_users += PERCENT_100;
      }
   });

} FC_CAPTURE_AND_RETHROW() }



void database::adjust_interface_users( const interface_object& interface, bool adjust = true )
{ try {
   time_point now = head_block_time();
   modify( interface, [&]( interface_object& i )
   {
      i.decay_weights( now );
      if( adjust )
      {
         i.daily_active_users += PERCENT_100;
         i.monthly_active_users += PERCENT_100;
      }
   });
} FC_CAPTURE_AND_RETHROW() }


void database::process_product_auctions()
{ try {
   time_point now = head_block_time();
   const auto& auction_idx = get_index< product_auction_sale_index >().indices().get< by_completion_time >();
   auto auction_itr = auction_idx.begin();
   const auto& bid_idx = get_index< product_auction_bid_index >().indices().get< by_highest_bid >();

   while( auction_itr != auction_idx.end() &&
      !auction_itr->completed &&
      auction_itr->completion_time <= now )
   {
      const product_auction_sale_object& auction = *auction_itr;
      ilog( "Processing Product Auction - Account: ${a} - ID: ${id} - Type: ${t} - Bid Count: ${c}",
         ("a",auction.account)("id",auction.auction_id )("t",auction.auction_type)("c",auction.bid_count));
      auto bid_itr = bid_idx.lower_bound( boost::make_tuple( auction.account, auction.auction_id, share_type::max(), product_auction_bid_id_type() ) );
      asset bid_price = auction.reserve_bid;
      
      if( bid_itr != bid_idx.end() )
      {
         const product_auction_bid_object& bid = *bid_itr;
         ilog( "Got Top auction bid: ${b}",("b",bid));

         if( bid.seller == auction.account &&
            bid.auction_id == auction.auction_id )
         {
            bid_price = asset( bid.public_bid_amount, auction.bid_asset() );

            if( auction.auction_type == product_auction_type::CONCEALED_SECOND_PRICE_AUCTION )
            {
               ++bid_itr;

               if( bid_itr != bid_idx.end() )
               {
                  const product_auction_bid_object& second_bid = *bid_itr;
                  ilog( "Got Second Price Auction bid: ${b}",("b",second_bid));

                  if( second_bid.seller == auction.account &&
                     second_bid.auction_id == auction.auction_id )
                  {
                     bid_price = asset( second_bid.public_bid_amount, auction.bid_asset() );
                  }
               }
            }
           
            const escrow_object& escrow = create< escrow_object >([&]( escrow_object& esc )
            {
               esc.from = bid.buyer;
               esc.to = bid.seller;
               esc.from_mediator = account_name_type();
               esc.to_mediator = account_name_type();
               esc.payment = bid_price + bid.delivery_value;
               esc.balance = asset( 0, bid.bid_asset );
               esc.escrow_id = bid.bid_id;
               esc.memo = bid.memo;
               esc.json = bid.json;
               esc.acceptance_time = now + fc::days(7);
               esc.escrow_expiration = now + fc::days(14);
               esc.dispute_release_time = time_point::maximum();
               esc.approvals[ bid.buyer ] = false;
               esc.approvals[ bid.seller ] = false;
               esc.created = now;
               esc.last_updated = now;
            });

            modify( bid, [&]( product_auction_bid_object& pabo )
            {
               pabo.winning_bid = true;
               pabo.last_updated = now;
            });

            modify( auction, [&]( product_auction_sale_object& paso )
            {
               paso.winning_bidder = bid.buyer;
               paso.winning_bid_id = bid.bid_id;
            });

            ilog( "Winning Bid from Buyer: ${w} at Bid Price: ${p}: ${b} \n Created escrow: ${e} \n",
               ("w",bid.buyer)("p",bid_price)("b",bid.bid_id)("e",escrow));
         }
      }

      modify( auction, [&]( product_auction_sale_object& paso )
      {
         paso.completed = true;
         paso.last_updated = now;
      });

      ++auction_itr;
   }
} FC_CAPTURE_AND_RETHROW() }


/**
 * Distributes currency issuance of all currency assets.
 * Pays out Staked and liquid Currency assets, including MEC, 
 * every block to all network contributors.
 * 
 * For MeCoin, the issuance rate is one Billion per year.
 * 
 *  - 25% of issuance is directed to Content Creator rewards.
 *  - 20% of issuance is directed to Equity Holder rewards.
 *  - 20% of issuance is directed to Block producers.
 *  - 10% of issuance is directed to Supernode Operator rewards.
 *  - 10% of issuance is directed to Staked MeCoin Holder rewards.
 *  -  5% of issuance is directed to The Community Enterprise fund.
 *  - 2.5% of issuance is directed to The Development reward pool.
 *  - 2.5% of issuance is directed to The Marketing reward pool.
 *  - 2.5% of issuance is directed to The Advocacy reward pool.
 *  - 2.5% of issuance is directed to The Activity reward pool.
 */
void database::process_funds()
{ try {
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   const producer_object& current_producer = get_producer( props.current_producer );
   const account_object& producer_account = get_account( props.current_producer );

   FC_ASSERT( current_producer.active && 
      producer_account.active,
      "Block Producer cannot process funds while account or producer object is inactive." );

   if( props.head_block_number > 0 )      // First block uses init genesis for reward.
   {
      const auto& currency_idx = get_index< asset_currency_data_index >().indices().get< by_id >();
      auto currency_itr = currency_idx.begin();

      while( currency_itr != currency_idx.end() )
      {
         const asset_currency_data_object& currency = *currency_itr;

         asset block_reward = currency.block_reward;

         asset content_reward            = ( block_reward * currency.content_reward_percent               ) / PERCENT_100;
         asset equity_reward             = ( block_reward * currency.equity_reward_percent                ) / PERCENT_100;
         asset producer_reward           = ( block_reward * currency.producer_reward_percent              ) / PERCENT_100;
         asset supernode_reward          = ( block_reward * currency.supernode_reward_percent             ) / PERCENT_100;
         asset power_reward              = ( block_reward * currency.power_reward_percent                 ) / PERCENT_100;
         asset community_fund_reward     = ( block_reward * currency.community_fund_reward_percent        ) / PERCENT_100;
         asset development_reward        = ( block_reward * currency.development_reward_percent           ) / PERCENT_100;
         asset marketing_reward          = ( block_reward * currency.marketing_reward_percent             ) / PERCENT_100;
         asset advocacy_reward           = ( block_reward * currency.advocacy_reward_percent              ) / PERCENT_100;
         asset activity_reward           = ( block_reward * currency.activity_reward_percent              ) / PERCENT_100;

         asset producer_block_reward     = ( producer_reward * currency.producer_block_reward_percent     ) / PERCENT_100;
         asset validation_reward         = ( producer_reward * currency.validation_reward_percent         ) / PERCENT_100;
         asset txn_stake_reward          = ( producer_reward * currency.txn_stake_reward_percent          ) / PERCENT_100;
         asset work_reward               = ( producer_reward * currency.work_reward_percent               ) / PERCENT_100;
         asset producer_activity_reward  = ( producer_reward * currency.producer_activity_reward_percent  ) / PERCENT_100;

         asset reward_checksum = content_reward + equity_reward + producer_reward + supernode_reward + power_reward + community_fund_reward + development_reward + marketing_reward + advocacy_reward + activity_reward;
         
         FC_ASSERT( reward_checksum == block_reward,
            "Block reward issuance checksum failed: ${r} != ${c}, for currency: ${c}",
            ("r", reward_checksum)("b", block_reward)("c", currency));

         asset producer_checksum = producer_block_reward + validation_reward + txn_stake_reward + work_reward + producer_activity_reward;
         
         FC_ASSERT( producer_checksum == producer_reward,
            "Producer reward issuance checksum failed: ${r} != ${c}, for currency: ${c}",
            ("r", producer_checksum)("b", producer_reward)("c", currency));
         
         const reward_fund_object& reward_fund = get_reward_fund( currency.symbol );
         const asset_equity_data_object& equity = get_equity_data( currency.equity_asset );

         modify( reward_fund, [&]( reward_fund_object& rfo )
         {
            rfo.adjust_content_reward_balance( content_reward );
            rfo.adjust_validation_reward_balance( validation_reward );
            rfo.adjust_txn_stake_reward_balance( txn_stake_reward );
            rfo.adjust_work_reward_balance( work_reward );
            rfo.adjust_producer_activity_reward_balance( producer_activity_reward );
            rfo.adjust_supernode_reward_balance( supernode_reward );
            rfo.adjust_power_reward_balance( power_reward );
            rfo.adjust_community_fund_balance( community_fund_reward );
            rfo.adjust_development_reward_balance( development_reward );
            rfo.adjust_marketing_reward_balance( marketing_reward );
            rfo.adjust_advocacy_reward_balance( advocacy_reward );
            rfo.adjust_activity_reward_balance( activity_reward );
         });

         modify( equity, [&]( asset_equity_data_object& aedo )
         {
            aedo.adjust_pool( equity_reward );
         });

         adjust_reward_balance( producer_account, producer_block_reward );

         asset producer_pending = validation_reward + txn_stake_reward + work_reward + producer_activity_reward;
         asset pending_issuance = content_reward + equity_reward + supernode_reward + power_reward + community_fund_reward + development_reward + marketing_reward + advocacy_reward + activity_reward;

         adjust_pending_supply( pending_issuance + producer_pending );
         
         push_virtual_operation( producer_reward_operation( producer_account.name, producer_block_reward ) );

         if( currency.block_reward_reduction_days > 0 && currency.block_reward.amount.value > 0 )     // Reduce Currency Block reward if block interval reached
         {
            if( props.head_block_number % ( currency.block_reward_reduction_days * BLOCKS_PER_DAY ) == 0 )
            {
               modify( currency, [&]( asset_currency_data_object& acdo )
               {
                  acdo.block_reward.amount -= ( ( acdo.block_reward.amount * currency.block_reward_reduction_percent ) / PERCENT_100 );
               });
            }
         }

         ++currency_itr;
      }
   }
} FC_CAPTURE_AND_RETHROW() }


void database::initialize_evaluators()
{
   // Account Evaluators

   _my->_evaluator_registry.register_evaluator< account_create_evaluator                 >();
   _my->_evaluator_registry.register_evaluator< account_update_evaluator                 >();
   _my->_evaluator_registry.register_evaluator< account_verification_evaluator           >();
   _my->_evaluator_registry.register_evaluator< account_business_evaluator               >();
   _my->_evaluator_registry.register_evaluator< account_membership_evaluator             >();
   _my->_evaluator_registry.register_evaluator< account_vote_executive_evaluator         >();
   _my->_evaluator_registry.register_evaluator< account_vote_officer_evaluator           >();
   _my->_evaluator_registry.register_evaluator< account_member_request_evaluator         >();
   _my->_evaluator_registry.register_evaluator< account_member_invite_evaluator          >();
   _my->_evaluator_registry.register_evaluator< account_accept_request_evaluator         >();
   _my->_evaluator_registry.register_evaluator< account_accept_invite_evaluator          >();
   _my->_evaluator_registry.register_evaluator< account_remove_member_evaluator          >();
   _my->_evaluator_registry.register_evaluator< account_update_list_evaluator            >();
   _my->_evaluator_registry.register_evaluator< account_producer_vote_evaluator          >();
   _my->_evaluator_registry.register_evaluator< account_update_proxy_evaluator           >();
   _my->_evaluator_registry.register_evaluator< request_account_recovery_evaluator       >();
   _my->_evaluator_registry.register_evaluator< recover_account_evaluator                >();
   _my->_evaluator_registry.register_evaluator< reset_account_evaluator                  >();
   _my->_evaluator_registry.register_evaluator< set_reset_account_evaluator              >();
   _my->_evaluator_registry.register_evaluator< change_recovery_account_evaluator        >();
   _my->_evaluator_registry.register_evaluator< decline_voting_rights_evaluator          >();
   _my->_evaluator_registry.register_evaluator< connection_request_evaluator             >();
   _my->_evaluator_registry.register_evaluator< connection_accept_evaluator              >();
   _my->_evaluator_registry.register_evaluator< account_follow_evaluator                 >();
   _my->_evaluator_registry.register_evaluator< tag_follow_evaluator                     >();
   _my->_evaluator_registry.register_evaluator< activity_reward_evaluator                >();

   // Network Evaluators

   _my->_evaluator_registry.register_evaluator< update_network_officer_evaluator         >();
   _my->_evaluator_registry.register_evaluator< network_officer_vote_evaluator           >();
   _my->_evaluator_registry.register_evaluator< update_executive_board_evaluator         >();
   _my->_evaluator_registry.register_evaluator< executive_board_vote_evaluator           >();
   _my->_evaluator_registry.register_evaluator< update_governance_evaluator              >();
   _my->_evaluator_registry.register_evaluator< subscribe_governance_evaluator           >();
   _my->_evaluator_registry.register_evaluator< update_supernode_evaluator               >();
   _my->_evaluator_registry.register_evaluator< update_interface_evaluator               >();
   _my->_evaluator_registry.register_evaluator< update_mediator_evaluator                >();
   _my->_evaluator_registry.register_evaluator< create_community_enterprise_evaluator    >();
   _my->_evaluator_registry.register_evaluator< claim_enterprise_milestone_evaluator     >();
   _my->_evaluator_registry.register_evaluator< approve_enterprise_milestone_evaluator   >();

   // Comment Evaluators

   _my->_evaluator_registry.register_evaluator< comment_evaluator                        >();
   _my->_evaluator_registry.register_evaluator< message_evaluator                        >();
   _my->_evaluator_registry.register_evaluator< vote_evaluator                           >();
   _my->_evaluator_registry.register_evaluator< view_evaluator                           >();
   _my->_evaluator_registry.register_evaluator< share_evaluator                          >();
   _my->_evaluator_registry.register_evaluator< moderation_tag_evaluator                 >();
   _my->_evaluator_registry.register_evaluator< list_evaluator                           >();
   _my->_evaluator_registry.register_evaluator< poll_evaluator                           >();
   _my->_evaluator_registry.register_evaluator< poll_vote_evaluator                      >();

   // Community Evaluators

   _my->_evaluator_registry.register_evaluator< community_create_evaluator               >();
   _my->_evaluator_registry.register_evaluator< community_update_evaluator               >();
   _my->_evaluator_registry.register_evaluator< community_add_mod_evaluator              >();
   _my->_evaluator_registry.register_evaluator< community_add_admin_evaluator            >();
   _my->_evaluator_registry.register_evaluator< community_vote_mod_evaluator             >();
   _my->_evaluator_registry.register_evaluator< community_transfer_ownership_evaluator   >();
   _my->_evaluator_registry.register_evaluator< community_join_request_evaluator         >();
   _my->_evaluator_registry.register_evaluator< community_join_accept_evaluator          >();
   _my->_evaluator_registry.register_evaluator< community_join_invite_evaluator          >();
   _my->_evaluator_registry.register_evaluator< community_invite_accept_evaluator        >();
   _my->_evaluator_registry.register_evaluator< community_remove_member_evaluator        >();
   _my->_evaluator_registry.register_evaluator< community_blacklist_evaluator            >();
   _my->_evaluator_registry.register_evaluator< community_subscribe_evaluator            >();
   _my->_evaluator_registry.register_evaluator< community_event_evaluator                >();
   _my->_evaluator_registry.register_evaluator< community_event_attend_evaluator         >();

   // Advertising Evaluators

   _my->_evaluator_registry.register_evaluator< ad_creative_evaluator                    >();
   _my->_evaluator_registry.register_evaluator< ad_campaign_evaluator                    >();
   _my->_evaluator_registry.register_evaluator< ad_inventory_evaluator                   >();
   _my->_evaluator_registry.register_evaluator< ad_audience_evaluator                    >();
   _my->_evaluator_registry.register_evaluator< ad_bid_evaluator                         >();

   // Graph Data Evaluators

   _my->_evaluator_registry.register_evaluator< graph_node_evaluator                     >();
   _my->_evaluator_registry.register_evaluator< graph_edge_evaluator                     >();
   _my->_evaluator_registry.register_evaluator< graph_node_property_evaluator            >();
   _my->_evaluator_registry.register_evaluator< graph_edge_property_evaluator            >();

   // Transfer Evaluators

   _my->_evaluator_registry.register_evaluator< transfer_evaluator                       >();
   _my->_evaluator_registry.register_evaluator< transfer_request_evaluator               >();
   _my->_evaluator_registry.register_evaluator< transfer_accept_evaluator                >();
   _my->_evaluator_registry.register_evaluator< transfer_recurring_evaluator             >();
   _my->_evaluator_registry.register_evaluator< transfer_recurring_request_evaluator     >();
   _my->_evaluator_registry.register_evaluator< transfer_recurring_accept_evaluator      >();
   _my->_evaluator_registry.register_evaluator< transfer_confidential_evaluator          >();
   _my->_evaluator_registry.register_evaluator< transfer_to_confidential_evaluator       >();
   _my->_evaluator_registry.register_evaluator< transfer_from_confidential_evaluator     >();

   // Balance Evaluators

   _my->_evaluator_registry.register_evaluator< claim_reward_balance_evaluator           >();
   _my->_evaluator_registry.register_evaluator< stake_asset_evaluator                    >();
   _my->_evaluator_registry.register_evaluator< unstake_asset_evaluator                  >();
   _my->_evaluator_registry.register_evaluator< unstake_asset_route_evaluator            >();
   _my->_evaluator_registry.register_evaluator< transfer_to_savings_evaluator            >();
   _my->_evaluator_registry.register_evaluator< transfer_from_savings_evaluator          >();
   _my->_evaluator_registry.register_evaluator< delegate_asset_evaluator                 >();
   
   // Marketplace Evaluators
   
   _my->_evaluator_registry.register_evaluator< product_sale_evaluator                   >();
   _my->_evaluator_registry.register_evaluator< product_purchase_evaluator               >();
   _my->_evaluator_registry.register_evaluator< product_auction_sale_evaluator           >();
   _my->_evaluator_registry.register_evaluator< product_auction_bid_evaluator            >();
   _my->_evaluator_registry.register_evaluator< escrow_transfer_evaluator                >();
   _my->_evaluator_registry.register_evaluator< escrow_approve_evaluator                 >();
   _my->_evaluator_registry.register_evaluator< escrow_dispute_evaluator                 >();
   _my->_evaluator_registry.register_evaluator< escrow_release_evaluator                 >();
   
   // Trading Evaluators

   _my->_evaluator_registry.register_evaluator< limit_order_evaluator                    >();
   _my->_evaluator_registry.register_evaluator< margin_order_evaluator                   >();
   _my->_evaluator_registry.register_evaluator< auction_order_evaluator                  >();
   _my->_evaluator_registry.register_evaluator< call_order_evaluator                     >();
   _my->_evaluator_registry.register_evaluator< option_order_evaluator                   >();

   // Pool Evaluators
   
   _my->_evaluator_registry.register_evaluator< liquidity_pool_create_evaluator          >();
   _my->_evaluator_registry.register_evaluator< liquidity_pool_exchange_evaluator        >();
   _my->_evaluator_registry.register_evaluator< liquidity_pool_fund_evaluator            >();
   _my->_evaluator_registry.register_evaluator< liquidity_pool_withdraw_evaluator        >();
   _my->_evaluator_registry.register_evaluator< credit_pool_collateral_evaluator         >();
   _my->_evaluator_registry.register_evaluator< credit_pool_borrow_evaluator             >();
   _my->_evaluator_registry.register_evaluator< credit_pool_lend_evaluator               >();
   _my->_evaluator_registry.register_evaluator< credit_pool_withdraw_evaluator           >();
   _my->_evaluator_registry.register_evaluator< option_pool_create_evaluator             >();
   _my->_evaluator_registry.register_evaluator< prediction_pool_create_evaluator         >();
   _my->_evaluator_registry.register_evaluator< prediction_pool_exchange_evaluator       >();
   _my->_evaluator_registry.register_evaluator< prediction_pool_resolve_evaluator        >();
   
   // Asset Evaluators

   _my->_evaluator_registry.register_evaluator< asset_create_evaluator                   >();
   _my->_evaluator_registry.register_evaluator< asset_update_evaluator                   >();
   _my->_evaluator_registry.register_evaluator< asset_issue_evaluator                    >();
   _my->_evaluator_registry.register_evaluator< asset_reserve_evaluator                  >();
   _my->_evaluator_registry.register_evaluator< asset_update_issuer_evaluator            >();
   _my->_evaluator_registry.register_evaluator< asset_distribution_evaluator             >();
   _my->_evaluator_registry.register_evaluator< asset_distribution_fund_evaluator        >();
   _my->_evaluator_registry.register_evaluator< asset_option_exercise_evaluator          >();
   _my->_evaluator_registry.register_evaluator< asset_stimulus_fund_evaluator            >();
   _my->_evaluator_registry.register_evaluator< asset_update_feed_producers_evaluator    >();
   _my->_evaluator_registry.register_evaluator< asset_publish_feed_evaluator             >();
   _my->_evaluator_registry.register_evaluator< asset_settle_evaluator                   >();
   _my->_evaluator_registry.register_evaluator< asset_global_settle_evaluator            >();
   _my->_evaluator_registry.register_evaluator< asset_collateral_bid_evaluator           >();
   
   // Block Producer Evaluators

   _my->_evaluator_registry.register_evaluator< producer_update_evaluator                >();
   _my->_evaluator_registry.register_evaluator< proof_of_work_evaluator                  >();
   _my->_evaluator_registry.register_evaluator< verify_block_evaluator                   >();
   _my->_evaluator_registry.register_evaluator< commit_block_evaluator                   >();
   _my->_evaluator_registry.register_evaluator< producer_violation_evaluator             >();

   // Custom Evaluators

   _my->_evaluator_registry.register_evaluator< custom_evaluator                         >();
   _my->_evaluator_registry.register_evaluator< custom_json_evaluator                    >();
}


void database::set_custom_operation_interpreter( const std::string& id, std::shared_ptr< custom_operation_interpreter > registry )
{
   bool inserted = _custom_operation_interpreters.emplace( id, registry ).second;
   // This assert triggering means we're mis-configured (multiple registrations of custom JSON evaluator for same ID)
   FC_ASSERT( inserted );
}

std::shared_ptr< custom_operation_interpreter > database::get_custom_json_evaluator( const std::string& id )
{
   auto it = _custom_operation_interpreters.find( id );
   if( it != _custom_operation_interpreters.end() )
      return it->second;
   return std::shared_ptr< custom_operation_interpreter >();
}

void database::initialize_indexes()
{
   // Global Indexes

   add_core_index< dynamic_global_property_index           >(*this);
   add_core_index< median_chain_property_index             >(*this);
   add_core_index< transaction_index                       >(*this);
   add_core_index< operation_index                         >(*this);
   add_core_index< reward_fund_index                       >(*this);
   add_core_index< block_summary_index                     >(*this);
   add_core_index< hardfork_property_index                 >(*this);
   
   // Account Indexes

   add_core_index< account_index                           >(*this);
   add_core_index< account_verification_index              >(*this);
   add_core_index< account_business_index                  >(*this);
   add_core_index< account_executive_vote_index            >(*this);
   add_core_index< account_officer_vote_index              >(*this);
   add_core_index< account_member_request_index            >(*this);
   add_core_index< account_member_invite_index             >(*this);
   add_core_index< account_member_key_index                >(*this);
   add_core_index< account_authority_index                 >(*this);
   add_core_index< account_permission_index                >(*this);
   add_core_index< account_following_index                 >(*this);
   add_core_index< account_balance_index                   >(*this);
   add_core_index< account_vesting_balance_index           >(*this);
   add_core_index< account_history_index                   >(*this);
   add_core_index< tag_following_index                     >(*this);
   add_core_index< connection_index                        >(*this);
   add_core_index< connection_request_index                >(*this);
   add_core_index< owner_authority_history_index           >(*this);
   add_core_index< account_recovery_request_index          >(*this);
   add_core_index< change_recovery_account_request_index   >(*this);
   add_core_index< decline_voting_rights_request_index     >(*this);

   // Network Indexes

   add_core_index< network_officer_index                   >(*this);
   add_core_index< network_officer_vote_index              >(*this);
   add_core_index< executive_board_index                   >(*this);
   add_core_index< executive_board_vote_index              >(*this);
   add_core_index< governance_account_index                >(*this);
   add_core_index< governance_subscription_index           >(*this);
   add_core_index< supernode_index                         >(*this);
   add_core_index< interface_index                         >(*this);
   add_core_index< mediator_index                          >(*this);
   add_core_index< community_enterprise_index              >(*this);
   add_core_index< enterprise_approval_index               >(*this);

   // Comment Indexes

   add_core_index< comment_index                           >(*this);
   add_core_index< comment_vote_index                      >(*this);
   add_core_index< comment_view_index                      >(*this);
   add_core_index< comment_share_index                     >(*this);
   add_core_index< moderation_tag_index                    >(*this);
   add_core_index< comment_metrics_index                   >(*this);
   add_core_index< message_index                           >(*this);
   add_core_index< list_index                              >(*this);
   add_core_index< poll_index                              >(*this);
   add_core_index< poll_vote_index                         >(*this);
   add_core_index< blog_index                              >(*this);
   add_core_index< feed_index                              >(*this);

   // Community Indexes

   add_core_index< community_index                         >(*this);
   add_core_index< community_member_index                  >(*this);
   add_core_index< community_member_key_index              >(*this);
   add_core_index< community_moderator_vote_index          >(*this);
   add_core_index< community_join_request_index            >(*this);
   add_core_index< community_join_invite_index             >(*this);
   add_core_index< community_event_index                   >(*this);

   // Advertising Indexes

   add_core_index< ad_creative_index                       >(*this);
   add_core_index< ad_campaign_index                       >(*this);
   add_core_index< ad_inventory_index                      >(*this);
   add_core_index< ad_audience_index                       >(*this);
   add_core_index< ad_bid_index                            >(*this);

   // Graph Data Indexes

   add_core_index< graph_node_index                        >(*this);
   add_core_index< graph_edge_index                        >(*this);
   add_core_index< graph_node_property_index               >(*this);
   add_core_index< graph_edge_property_index               >(*this);

   // Transfer Indexes

   add_core_index< transfer_request_index                  >(*this);
   add_core_index< transfer_recurring_index                >(*this);
   add_core_index< transfer_recurring_request_index        >(*this);

   // Balance Indexes

   add_core_index< unstake_asset_route_index               >(*this);
   add_core_index< savings_withdraw_index                  >(*this);
   add_core_index< asset_delegation_index                  >(*this);
   add_core_index< asset_delegation_expiration_index       >(*this);
   add_core_index< confidential_balance_index              >(*this);

   // Marketplace Indexes

   add_core_index< product_sale_index                      >(*this);
   add_core_index< product_purchase_index                  >(*this);
   add_core_index< product_auction_sale_index              >(*this);
   add_core_index< product_auction_bid_index               >(*this);
   add_core_index< escrow_index                            >(*this);

   // Trading Indexes

   add_core_index< limit_order_index                       >(*this);
   add_core_index< margin_order_index                      >(*this);
   add_core_index< auction_order_index                     >(*this);
   add_core_index< call_order_index                        >(*this);
   add_core_index< option_order_index                      >(*this);
   
   // Asset Indexes

   add_core_index< asset_index                             >(*this);
   add_core_index< asset_dynamic_data_index                >(*this);
   add_core_index< asset_currency_data_index               >(*this);
   add_core_index< asset_stablecoin_data_index             >(*this);
   add_core_index< asset_settlement_index                  >(*this);
   add_core_index< asset_collateral_bid_index              >(*this);
   add_core_index< asset_equity_data_index                 >(*this);
   add_core_index< asset_bond_data_index                   >(*this);
   add_core_index< asset_credit_data_index                 >(*this);
   add_core_index< asset_stimulus_data_index               >(*this);
   add_core_index< asset_unique_data_index                 >(*this);
   add_core_index< asset_liquidity_pool_index              >(*this);
   add_core_index< asset_credit_pool_index                 >(*this);
   add_core_index< asset_option_pool_index                 >(*this);
   add_core_index< asset_prediction_pool_index             >(*this);
   add_core_index< asset_prediction_pool_resolution_index  >(*this);
   add_core_index< asset_distribution_index                >(*this);
   add_core_index< asset_distribution_balance_index        >(*this);

   // Credit Indexes

   add_core_index< credit_collateral_index                 >(*this);
   add_core_index< credit_loan_index                       >(*this);

   // Block Producer Objects 

   add_core_index< producer_index                          >(*this);
   add_core_index< producer_schedule_index                 >(*this);
   add_core_index< producer_vote_index                     >(*this);
   add_core_index< block_validation_index                  >(*this);
   add_core_index< commit_violation_index                  >(*this);

   _plugin_index_signal();
}

const std::string& database::get_json_schema()const
{
   return _json_schema;
}


void database::validate_transaction( const signed_transaction& trx )
{
   database::with_write_lock( [&]()
   {
      auto session = start_undo_session( true );
      _apply_transaction( trx );
      session.undo();
   });
}

void database::set_flush_interval( uint32_t flush_blocks )
{
   _flush_blocks = flush_blocks;
   _next_flush_block = 0;
}

//////////////////// private methods ////////////////////

void database::apply_block( const signed_block& next_block, uint32_t skip )
{ try {
   //fc::time_point begin_time = fc::time_point::now();

   auto block_num = next_block.block_num();
   if( _checkpoints.size() && _checkpoints.rbegin()->second != block_id_type() )
   {
      auto itr = _checkpoints.find( block_num );

      if( itr != _checkpoints.end() )
      {
         FC_ASSERT( next_block.id() == itr->second, "Block did not match checkpoint", ("checkpoint",*itr)("block_id",next_block.id()) );
      }
         

      if( _checkpoints.rbegin()->first >= block_num )
      {
         skip = skip_producer_signature
              | skip_transaction_signatures
              | skip_transaction_dupe_check
              | skip_fork_db
              | skip_block_size_check
              | skip_tapos_check
              | skip_authority_check
              | skip_merkle_check //While blockchain is being downloaded, txs need to be validated against block headers
              | skip_undo_history_check
              | skip_producer_schedule_check
              | skip_validate
              | skip_validate_invariants
              ;
      }  
   }

   detail::with_skip_flags( *this, skip, [&]()
   {
      _apply_block( next_block );
   });

   /*try
   {
   /// check invariants
   if( is_producing() || !( skip & skip_validate_invariants ) )
      validate_invariants();
   }
   FC_CAPTURE_AND_RETHROW( (next_block) );*/

   //fc::time_point end_time = fc::time_point::now();
   //fc::microseconds dt = end_time - begin_time;
   if( _flush_blocks != 0 )
   {
      if( _next_flush_block == 0 )
      {
         uint64_t lep = block_num + 1 + _flush_blocks * 9 / 10;
         uint64_t rep = block_num + 1 + _flush_blocks;

         // use time_point::now() as RNG source to pick block randomly between lep and rep
         uint64_t span = rep - lep;
         uint64_t x = lep;
         if( span > 0 )
         {
            uint64_t now = uint64_t( fc::time_point::now().time_since_epoch().count() );
            x += now % span;
         }
         _next_flush_block = x;
         //ilog( "Next flush scheduled at block ${b}", ("b", x) );
      }

      if( _next_flush_block == block_num )
      {
         _next_flush_block = 0;
         //ilog( "Flushing database shared memory at block ${b}", ("b", block_num) );
         chainbase::database::flush();
      }
   }

   show_free_memory( false );

} FC_CAPTURE_AND_RETHROW( (next_block) ) }

void database::show_free_memory( bool force )
{
   uint64_t free_gb = uint64_t( get_free_memory() / (1024*1024*1024) );
   if( force || (free_gb < _last_free_gb_printed) || (free_gb > _last_free_gb_printed+1) )
   {
      ilog( "Free memory is now ${n} GB", ("n", free_gb) );
      _last_free_gb_printed = free_gb;
   }

   if( free_gb == 0 )
   {
      uint64_t free_mb = uint64_t( get_free_memory() / (1024*1024) );

      if( free_mb <= 50 && head_block_num() % 1000 == 0 )
      {
         elog( "Free memory is now ${n} MB. Shared Memory Capacity is insufficient, and may cause a node failure when depleted. Please increase shared file size.", 
         ("n", free_mb) );
      } 
   }
}

void database::_apply_block( const signed_block& next_block )
{ try {
   // ilog( "Begin Applying Block: ${b}", ("b", next_block.id() ) );
   notify_pre_apply_block( next_block );
   uint64_t next_block_num = next_block.block_num();
   uint32_t skip = get_node_properties().skip_flags;

   if( !( skip & skip_merkle_check ) )
   {
      auto merkle_root = next_block.calculate_merkle_root();

      try
      {
         FC_ASSERT( next_block.transaction_merkle_root == merkle_root, 
            "Merkle check failed", ("next_block.transaction_merkle_root",next_block.transaction_merkle_root)("calc",merkle_root)("next_block",next_block)("id",next_block.id()) );
      }
      catch( fc::assert_exception& e )
      {
         const auto& merkle_map = get_shared_db_merkle();
         auto itr = merkle_map.find( next_block_num );

         if( itr == merkle_map.end() || itr->second != merkle_root )
         {
            throw e;
         }  
      }
   }

   const producer_object& signing_producer = validate_block_header( skip, next_block );

   _current_block_num = next_block_num;
   _current_trx_in_block = 0;
   _current_trx_stake_weight = 0;

   const dynamic_global_property_object& props = get_dynamic_global_properties();
   const median_chain_property_object& median_props = get_median_chain_properties();
   
   size_t block_size = fc::raw::pack_size( next_block );

   FC_ASSERT( block_size <= median_props.maximum_block_size, 
      "Block Size is too large.", 
      ("next_block_num",next_block_num)("block_size", block_size)("max",median_props.maximum_block_size ) );
   
   if( block_size < MIN_BLOCK_SIZE )
   {
      elog( "Block size is too small",
         ("next_block_num",next_block_num)("block_size", block_size)("min",MIN_BLOCK_SIZE)
      );
   }

   // Modify current producer so transaction evaluators can know who included the transaction,
   // this is mostly for POW operations which must pay the current_producer.

   modify( props, [&]( dynamic_global_property_object& dgp )
   {
      dgp.current_producer = next_block.producer;
   });

   process_header_extensions( next_block );     // parse producer version reporting

   const producer_object& producer = get_producer( next_block.producer );
   const hardfork_property_object& hardfork_state = get_hardfork_property_object();

   FC_ASSERT( producer.running_version >= hardfork_state.current_hardfork_version,
      "Block produced by producer that is not running current hardfork.",
      ("producer",producer)("next_block.producer",next_block.producer)("hardfork_state", hardfork_state)
   );
   
   /** 
    * We do not need to push the undo state for each transaction
    * because they either all apply and are valid or the
    * entire block fails to apply. We only need an "undo" state
    * for transactions when validating broadcast transactions or
    * when building a block.
    */
   for( const auto& trx : next_block.transactions )
   {
      apply_transaction( trx, skip );
      ++_current_trx_in_block;
   }

   update_global_dynamic_data( next_block );
   update_signing_producer( signing_producer, next_block );
   update_producer_schedule(*this);
   update_last_irreversible_block();
   update_transaction_stake( signing_producer, _current_trx_stake_weight );
   create_block_summary( next_block );

   clear_expired_transactions();
   clear_expired_operations();
   clear_expired_delegations();
   
   update_producer_set();
   update_governance_account_set();
   update_community_moderator_set();
   update_business_account_set();
   update_comment_metrics();
   update_message_counter();
   update_median_liquidity();
   update_proof_of_work_target();
   update_account_reputations();
   
   process_funds();

   process_asset_staking();
   process_stablecoins();
   process_savings_withdraws();
   process_recurring_transfers();
   process_equity_rewards();
   process_power_rewards();
   process_bond_interest();
   process_bond_assets();
   process_credit_updates();
   process_credit_buybacks();
   process_margin_updates();
   process_credit_interest();
   process_stimulus_assets();

   process_auction_orders();
   process_option_assets();
   process_prediction_assets();
   process_unique_assets();
   process_asset_distribution();
   process_product_auctions();

   process_membership_updates();
   process_txn_stake_rewards();
   process_validation_rewards();
   process_producer_activity_rewards();
   process_network_officer_rewards();
   process_executive_board_budgets();
   process_supernode_rewards();
   process_community_enterprise_fund();

   process_comment_cashout();
   
   account_recovery_processing();
   process_escrow_transfers();
   process_decline_voting_rights();
   process_hardforks();

   notify_applied_block( next_block );      // notify observers that the block has been applied
} FC_CAPTURE_LOG_AND_RETHROW( (next_block.block_num()) ) }


void database::process_header_extensions( const signed_block& next_block )
{
   auto itr = next_block.extensions.begin();

   while( itr != next_block.extensions.end() )
   {
      switch( itr->which() )
      {
         case 0: // void_t
            break;
         case 1: // version
         {
            auto reported_version = itr->get< version >();
            const auto& signing_producer = get_producer( next_block.producer );
            //idump( (next_block.producer)(signing_producer.running_version)(reported_version) );

            if( reported_version != signing_producer.running_version )
            {
               modify( signing_producer, [&]( producer_object& p )
               {
                  p.running_version = reported_version;
               });
            }
            break;
         }
         case 2: // hardfork_version vote
         {
            auto hfv = itr->get< hardfork_version_vote >();
            const auto& signing_producer = get_producer( next_block.producer );
            //idump( (next_block.producer)(signing_producer.running_version)(hfv) );

            if( hfv.hf_version != signing_producer.hardfork_version_vote || hfv.hf_time != signing_producer.hardfork_time_vote )
               modify( signing_producer, [&]( producer_object& p )
               {
                  p.hardfork_version_vote = hfv.hf_version;
                  p.hardfork_time_vote = hfv.hf_time;
               });

            break;
         }
         default:
            FC_ASSERT( false, "Unknown extension in block header" );
      }

      ++itr;
   }
}

void database::apply_transaction( const signed_transaction& trx, uint32_t skip )
{
   detail::with_skip_flags( *this, skip, [&]() { _apply_transaction(trx); });
   notify_on_applied_transaction( trx );
}

void database::_apply_transaction( const signed_transaction& trx )
{ try {
   _current_trx_id = trx.id();
   uint32_t skip = get_node_properties().skip_flags;

   if( !(skip&skip_validate) )
   {
      trx.validate();
   }

   auto& trx_idx = get_index< transaction_index >();
   const chain_id_type& chain_id = CHAIN_ID;
   auto trx_id = trx.id();

   // idump((trx_id)(skip&skip_transaction_dupe_check));

   FC_ASSERT( (skip & skip_transaction_dupe_check) ||
      trx_idx.indices().get< by_trx_id >().find( trx_id ) == trx_idx.indices().get< by_trx_id >().end(),
      "Duplicate transaction check failed", ("trx_ix", trx_id) );

   if( !(skip & (skip_transaction_signatures | skip_authority_check) ) )
   {
      auto get_active  = [&]( const string& name ) { return authority( get< account_authority_object, by_account >( name ).active_auth ); };
      auto get_owner   = [&]( const string& name ) { return authority( get< account_authority_object, by_account >( name ).owner_auth );  };
      auto get_posting = [&]( const string& name ) { return authority( get< account_authority_object, by_account >( name ).posting_auth );  };

      try
      {
         trx.verify_authority( chain_id, get_active, get_owner, get_posting, MAX_SIG_CHECK_DEPTH );
      }
      catch( protocol::tx_missing_active_auth& e )
      {
         if( get_shared_db_merkle().find( head_block_num() + 1 ) == get_shared_db_merkle().end() )
            throw e;
      }
   }

   // Skip all manner of expiration and TaPoS checking if we're on block 1; It's impossible that the transaction is
   // expired, and TaPoS makes no sense as no blocks exist yet.
   if( BOOST_LIKELY( head_block_num() > 0 ) )
   {
      if( !(skip & skip_tapos_check) )
      {
         const auto& tapos_block_summary = get< block_summary_object >( trx.ref_block_num );

         // Verify TaPoS block summary has correct ID prefix, and that this block's time is not past the expiration

         ASSERT( trx.ref_block_prefix == tapos_block_summary.block_id._hash[1], transaction_tapos_exception,
            "", ("trx.ref_block_prefix", trx.ref_block_prefix)
            ("tapos_block_summary",tapos_block_summary.block_id._hash[1]));
      }

      fc::time_point now = head_block_time();

      ASSERT( trx.expiration <= now + fc::seconds(MAX_TIME_UNTIL_EXPIRATION), transaction_expiration_exception,
         "", ("trx.expiration",trx.expiration)("now",now)("max_til_exp",MAX_TIME_UNTIL_EXPIRATION));
      
      ASSERT( now < trx.expiration, transaction_expiration_exception, "", ("now",now)("trx.exp",trx.expiration) );
   }

   //Insert transaction into unique transactions database.
   if( !(skip & skip_transaction_dupe_check) )
   {
      create< transaction_object >([&]( transaction_object& transaction )
      {
         transaction.trx_id = trx_id;
         transaction.expiration = trx.expiration;
         fc::raw::pack( transaction.packed_trx, trx );
      });
   }

   notify_on_pre_apply_transaction( trx );

   _current_op_in_trx = 0;
   for( const auto& op : trx.operations )
   { 
      try 
      {
         apply_operation( op );   // process the operations
         ++_current_op_in_trx;
      } FC_CAPTURE_AND_RETHROW( (op) );
   }

   check_flash_loans();       // Ensure no unresolved flash loans.
   update_stake( trx );       // Apply stake weight to the block producer.
   _current_trx_id = transaction_id_type();

} FC_CAPTURE_AND_RETHROW( (trx) ) }


/**
 * Checks the Credit loan index for any unresolved flash loans 
 * that have not been repaid in the same transaction.
 */
void database::check_flash_loans()
{ try {
   const auto& flash_idx = get_index< credit_loan_index >().indices().get< by_flash_loan >();
   auto flash_itr = flash_idx.lower_bound( true );

   if( flash_itr != flash_idx.end() )
   {
      const credit_loan_object& flash_loan = *flash_itr;
      
      FC_ASSERT( !flash_loan.flash_loan,
         "Transaction does not repay flash loan: ${l}.",
         ("l",flash_loan));
   }
} FC_CAPTURE_AND_RETHROW() }


void database::update_stake( const signed_transaction& trx )
{
   if( trx.operations.size() )
   {
      flat_set< account_name_type > creators;
      for( const operation& op : trx.operations )
      {
         operation_creator_name( op, creators );
      }
      share_type voting_power;
      for( account_name_type name : creators )
      {
         voting_power += get_voting_power( name );
      }
      size_t size = fc::raw::pack_size(trx);
      uint128_t stake_weight = approx_sqrt( uint128_t( ( voting_power.value / BLOCKCHAIN_PRECISION.value ) * size ) );
      _current_trx_stake_weight += stake_weight;
   }
}

/**
 * Decays and increments the current producer 
 * according to the stake weight of all the 
 * transactions in the block they have created.
 */
void database::update_transaction_stake( const producer_object& signing_producer, const uint128_t& transaction_stake )
{ try {
   const median_chain_property_object& median_props = get_median_chain_properties();
   const time_point now = head_block_time();
   fc::microseconds decay_time = median_props.txn_stake_decay_time;

   modify( signing_producer, [&]( producer_object& p )
   {
      p.recent_txn_stake_weight -= ( p.recent_txn_stake_weight * ( now - p.last_txn_stake_weight_update).to_seconds() ) / decay_time.to_seconds();
      p.recent_txn_stake_weight += transaction_stake;
      p.last_txn_stake_weight_update = now;
   });

} FC_CAPTURE_AND_RETHROW() }

void database::apply_operation(const operation& op)
{
   operation_notification note(op);
   notify_pre_apply_operation( note );
   _my->_evaluator_registry.get_evaluator( op ).apply( op );
   notify_post_apply_operation( note );
}

const producer_object& database::validate_block_header( uint32_t skip, const signed_block& next_block )const
{ try {
   FC_ASSERT( head_block_id() == next_block.previous,
      "Head Block ID must equal previous block header in new block.", 
      ("head_block_id",head_block_id())("next.prev",next_block.previous) );
   FC_ASSERT( head_block_time() < next_block.timestamp,
      "Head Block time must be less than timestamp of new block.",
      ("head_block_time",head_block_time())("next",next_block.timestamp)("blocknum",next_block.block_num()) );

   const producer_object& producer = get_producer( next_block.producer );

   if( !( skip&skip_producer_signature ) )
   {
      FC_ASSERT( next_block.validate_signee( producer.signing_key ) );
   }  

   if( !( skip&skip_producer_schedule_check ) )
   {
      uint64_t slot_num = get_slot_at_time( next_block.timestamp );
      FC_ASSERT( slot_num > 0,
         "slot number must be greater than 0." );

      string scheduled_producer = get_scheduled_producer( slot_num );

      FC_ASSERT( producer.owner == scheduled_producer, "producer produced block at wrong time",
         ("block producer",next_block.producer)("scheduled",scheduled_producer)("slot_num",slot_num) );
   }

   return producer;
} FC_CAPTURE_AND_RETHROW() }


void database::create_block_summary(const signed_block& next_block)
{ try {
   block_summary_id_type sid( next_block.block_num() & 0xffff );
   
   modify( get< block_summary_object >( sid ), [&]( block_summary_object& p ) 
   {
      p.block_id = next_block.id();
   });
} FC_CAPTURE_AND_RETHROW() }


void database::update_global_dynamic_data( const signed_block& b )
{ try {
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   
   uint32_t missed_blocks = 0;
   price equity_price = get_liquidity_pool( SYMBOL_COIN, SYMBOL_EQUITY ).hour_median_price;
   price usd_price = get_liquidity_pool( SYMBOL_COIN, SYMBOL_USD ).hour_median_price;

   if( head_block_time() != fc::time_point() )
   {
      missed_blocks = get_slot_at_time( b.timestamp );
      assert( missed_blocks != 0 );
      missed_blocks--;
      for( uint32_t i = 0; i < missed_blocks; ++i )
      {
         const auto& producer_missed = get_producer( get_scheduled_producer( i + 1 ) );
         if( producer_missed.owner != b.producer )
         {
            modify( producer_missed, [&]( producer_object& p )
            {
               p.total_missed++;
               if( ( head_block_num() - p.last_confirmed_block_num ) > BLOCKS_PER_DAY )
               {
                  p.active = false;
                  push_virtual_operation( shutdown_producer_operation( p.owner ) );
               }
            } );
         }
      }
   }
   
   modify( props, [&]( dynamic_global_property_object& dgpo )  
   {
      // Dynamic global properties updating, constant time assuming 100% participation. It is O(B) otherwise (B = Num blocks between update)

      for( uint32_t i = 0; i < missed_blocks + 1; i++ )
      {
         dgpo.participation_count -= dgpo.recent_slots_filled.hi & 0x8000000000000000ULL ? 1 : 0;
         dgpo.recent_slots_filled = ( dgpo.recent_slots_filled << 1 ) + ( i == 0 ? 1 : 0 );
         dgpo.participation_count += ( i == 0 ? 1 : 0 );
      }

      dgpo.head_block_number = b.block_num();
      dgpo.head_block_id = b.id();
      dgpo.time = b.timestamp;
      dgpo.current_aslot += (missed_blocks+1);
      dgpo.current_median_equity_price = equity_price;
      dgpo.current_median_usd_price = usd_price;
   });

   // ilog( "Updated Global Dynamic Data: Head Block Number: ${n} Head block ID: ${i} ASlot: ${s} Time: ${t}", 
   // ("n", props.head_block_number)("i", props.head_block_id)("s",props.current_aslot)("t",props.time) );

   if( !( get_node_properties().skip_flags & skip_undo_history_check ) )
   {
      ASSERT( props.head_block_number - props.last_irreversible_block_num  < MAX_UNDO_HISTORY, undo_database_exception,
         "The database does not have enough undo history to support a blockchain with so many missed blocks. "
         "Please add a checkpoint if you would like to continue applying blocks beyond this point.",
         ("last_irreversible_block_num",props.last_irreversible_block_num)("head", props.head_block_number)
         ("max_undo",MAX_UNDO_HISTORY) );
   }
} FC_CAPTURE_AND_RETHROW() }

void database::update_signing_producer( const producer_object& signing_producer, const signed_block& new_block )
{ try {
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   uint64_t new_block_aslot = props.current_aslot + get_slot_at_time( new_block.timestamp );

   modify( signing_producer, [&]( producer_object& p )
   {
      p.last_aslot = new_block_aslot;
      p.last_confirmed_block_num = new_block.block_num();
      p.total_blocks++;
   });
} FC_CAPTURE_AND_RETHROW() }


/**
 * Updates the last irreversible and last committed block numbers and IDs,
 * enabling nodes to add the block history to their block logs, when consensus finality 
 * is achieved by block producers.
 */
void database::update_last_irreversible_block()
{ try {
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   const producer_schedule_object& pso = get_producer_schedule();

   vector< const producer_object* > producer_objs;

   producer_objs.reserve( pso.num_scheduled_producers );

   for( size_t i = 0; i < pso.current_shuffled_producers.size(); i++ )
   {
      producer_objs.push_back( &get_producer( pso.current_shuffled_producers[ i ] ) );
   }

   static_assert( IRREVERSIBLE_THRESHOLD > 0, "irreversible threshold must be nonzero" );

   // 1 1 1 2 2 2 2 2 2 2 -> 2     .7*10 = 7
   // 1 1 1 1 1 1 1 2 2 2 -> 1
   // 3 3 3 3 3 3 3 3 3 3 -> 3

   size_t offset = ( ( PERCENT_100 - IRREVERSIBLE_THRESHOLD ) * producer_objs.size() / PERCENT_100 );

   std::nth_element( producer_objs.begin(), producer_objs.begin() + offset, producer_objs.end(),
      []( const producer_object* a, const producer_object* b )
      {
         return a->last_confirmed_block_num < b->last_confirmed_block_num;
      });

   uint64_t new_last_irreversible_block_num = producer_objs[offset]->last_confirmed_block_num;

   std::nth_element( producer_objs.begin(), producer_objs.begin() + offset, producer_objs.end(),
      []( const producer_object* a, const producer_object* b )
      {
         return a->last_commit_height < b->last_commit_height;
      });

   uint64_t new_last_committed_block_num = producer_objs[offset]->last_commit_height;

   if( new_last_irreversible_block_num > props.last_irreversible_block_num )
   {
      block_id_type irreversible_id = get_block_id_for_num( new_last_irreversible_block_num );

      modify( props, [&]( dynamic_global_property_object& d )
      {
         d.last_irreversible_block_num = new_last_irreversible_block_num;
         d.last_irreversible_block_id = irreversible_id;
      });
   }

   if( new_last_committed_block_num > props.last_committed_block_num )
   {
      block_id_type commit_id = get_block_id_for_num( new_last_committed_block_num );

      modify( props, [&]( dynamic_global_property_object& d )
      {
         d.last_committed_block_num = new_last_committed_block_num;
         d.last_committed_block_id = commit_id;
      });
   }

   // Take the highest of last committed and irreversible blocks, and commit it to the local database.
   uint64_t commit_height = std::max( props.last_committed_block_num, props.last_irreversible_block_num );
   
   commit( commit_height );  // Node will not reverse blocks after they have been committed or produced on by two thirds of producers.

   if( !( get_node_properties().skip_flags & skip_block_log ) )  // Output to block log based on new committed and last irreversiible block numbers.
   {
      const auto& tmp_head = _block_log.head();
      uint64_t log_head_num = 0;

      if( tmp_head )
      {
         log_head_num = tmp_head->block_num();
      }

      if( log_head_num < commit_height )
      {
         while( log_head_num < commit_height )
         {
            shared_ptr< fork_item > block = _fork_db.fetch_block_on_main_branch_by_number( log_head_num+1 );
            FC_ASSERT( block, "Current fork in the fork database does not contain the last_irreversible_block" );
            _block_log.append( block->data );
            log_head_num++;
         }

         _block_log.flush();
      }
   }

   _fork_db.set_max_size( props.head_block_number - commit_height + 1 );

} FC_CAPTURE_AND_RETHROW() }


asset database::calculate_issuer_fee( const asset_object& trade_asset, const asset& trade_amount )
{ try {
   FC_ASSERT( trade_asset.symbol == trade_amount.symbol,
   "Trade asset symbol must be equal to trade amount symbol." );
      
   if( trade_asset.market_fee_percent == 0 )
   {
      return asset( 0, trade_asset.symbol );
   }

   share_type value = (( trade_amount.amount * trade_asset.market_fee_percent ) / PERCENT_100  );
   asset percent_fee = asset( value, trade_asset.symbol );

   if( percent_fee.amount > trade_asset.max_market_fee )
   {
      percent_fee.amount = trade_asset.max_market_fee;
   }
      
   return percent_fee;
} FC_CAPTURE_AND_RETHROW() }

asset database::pay_issuer_fees( const asset_object& recv_asset, const asset& receives )
{ try {
   asset issuer_fees = calculate_issuer_fee( recv_asset, receives );

   FC_ASSERT( issuer_fees <= receives, 
      "Market fee shouldn't be greater than receives." );

   if( issuer_fees.amount > 0 )
   {
      adjust_reward_balance( recv_asset.issuer, issuer_fees );
   }

   return issuer_fees;
} FC_CAPTURE_AND_RETHROW() }


asset database::pay_issuer_fees( const account_object& seller, const asset_object& recv_asset, const asset& receives )
{ try {
   const asset& issuer_fees = calculate_issuer_fee( recv_asset, receives );
   FC_ASSERT( issuer_fees <= receives,
      "Market fee shouldn't be greater than receives." );
   
   if( issuer_fees.amount > 0 )
   {
      asset reward = asset( 0, recv_asset.symbol );
      asset reward_paid = asset( 0, recv_asset.symbol );

      uint16_t reward_percent = recv_asset.market_fee_share_percent;       // Percentage of market fees shared with registrars

      if( reward_percent > 0 )         // Calculate and pay market fee sharing rewards
      {
         const account_permission_object& issuer_permissions = get_account_permissions( seller.name );
         const account_permission_object& registrar_permissions = get_account_permissions( seller.registrar );
         const account_permission_object& referrer_permissions = get_account_permissions( seller.referrer );

         share_type reward_value = ( issuer_fees.amount * reward_percent ) / PERCENT_100;
         asset registrar_reward = asset( 0, recv_asset.symbol );
         asset referrer_reward = asset( 0, recv_asset.symbol );

         if( reward_value > 0 )
         {
            reward = asset( reward_value, recv_asset.symbol );

            FC_ASSERT( reward < issuer_fees,
               "Market reward should be less than issuer fees." );

            if( registrar_permissions.is_authorized_transfer( recv_asset.issuer, recv_asset ) &&
               issuer_permissions.is_authorized_transfer( seller.registrar, recv_asset ) )
            {
               registrar_reward = reward;          // Registrar begins with all reward
            }

            if( seller.referrer != seller.registrar )
            {
               share_type referrer_rewards_value;

               if( registrar_reward == reward )
               {
                  referrer_rewards_value = ( reward.amount * seller.referrer_rewards_percentage ) / PERCENT_100;
               }
               else
               {
                  referrer_rewards_value = reward.amount;         // Referrer gets all reward if registrar cannot receive.
               }
               
               FC_ASSERT ( referrer_rewards_value <= reward.amount.value,
                  "Referrer reward shouldn't be greater than total reward." );

               if( referrer_rewards_value > 0 )
               {
                  if( referrer_permissions.is_authorized_transfer( recv_asset.issuer, recv_asset ) &&
                     issuer_permissions.is_authorized_transfer( seller.referrer, recv_asset ) )
                  {
                     referrer_reward = asset( referrer_rewards_value, recv_asset.symbol );
                     registrar_reward -= referrer_reward;    // Referrer and registrar split reward;
                  }
               }  
            }

            if( registrar_reward.amount > 0 )
            {
               adjust_reward_balance( seller.registrar, registrar_reward );
               reward_paid += registrar_reward;
            }
            if( referrer_reward.amount > 0 )
            {
               adjust_reward_balance( seller.referrer, referrer_reward );
               reward_paid += referrer_reward;
            }
         }
      }

      adjust_reward_balance( recv_asset.issuer, issuer_fees - reward_paid );
   }

   return issuer_fees;
} FC_CAPTURE_AND_RETHROW() }


/**
 * Pays the network fee by burning the core asset into accumulated network revenue,
 * or by burning network credit assets or force settling USD assets if their price
 * falls below $1.00 USD.
 */
asset database::pay_network_fees( const asset& amount )
{ try {
   asset total_fees = amount;
   if( amount.symbol != SYMBOL_COIN )
   {
      total_fees = liquid_exchange( amount, SYMBOL_COIN, true, false );
   }
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   time_point now = head_block_time();
   price credit_usd_price = get_liquidity_pool( SYMBOL_USD, SYMBOL_CREDIT ).hour_median_price;
   price usd_settlement_price = get_stablecoin_data( SYMBOL_USD ).settlement_price;
   price usd_market_price = get_liquidity_pool( SYMBOL_COIN, SYMBOL_USD ).base_hour_median_price( usd_settlement_price.base.symbol );

   if( usd_market_price < usd_settlement_price )   // If the market price of USD is below settlement price
   {
      asset usd_purchased = liquid_exchange( total_fees, SYMBOL_USD, true, false );   // Liquid Exchange into USD, without paying fees to avoid recursive fees. 

      create< asset_settlement_object >([&]( asset_settlement_object& fso )
      {
         fso.owner = NULL_ACCOUNT;
         fso.balance = usd_purchased;    // Settle USD purchased at below settlement price, to increase total Coin burned.
         fso.settlement_date = now + fc::minutes( 10 );
      });
   }
   else if( credit_usd_price < price(asset(1,SYMBOL_USD)/asset(1,SYMBOL_CREDIT)) )   // If price of credit is below $1.00 USD
   {
      liquid_exchange( total_fees, SYMBOL_CREDIT, true, false );  // Liquid Exchange into Credit asset, without paying fees to avoid recursive fees. 

      modify( props, [&]( dynamic_global_property_object& gpo ) 
      {
         gpo.accumulated_network_revenue += total_fees;
      });
   }
   else   // Remove Coin from Supply and increment network revenue.
   {
      modify( props, [&]( dynamic_global_property_object& gpo ) 
      {
         gpo.accumulated_network_revenue += total_fees;
      });
   }

   return total_fees;

} FC_CAPTURE_AND_RETHROW() }


/**
 * Pays the network fee by burning the core asset into accumulated network revenue,
 * or by burning network credit assets or force settling USD assets if their price
 * falls below $1.00 USD. Splits revenue to registrar and referrer, and governance 
 * accounts that the user subscribes to.
 */
asset database::pay_network_fees( const account_object& payer, const asset& amount )
{ try {
   asset total_fees = amount;
   if( amount.symbol != SYMBOL_COIN )
   {
      total_fees = liquid_exchange( amount, SYMBOL_COIN, true, false );
   }
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   time_point now = head_block_time();

   flat_set<const account_object*> governance_subscriptions;

   const auto& g_idx = get_index< governance_subscription_index >().indices().get< by_account_governance >();
   auto g_itr = g_idx.lower_bound( payer.name );

   while( g_itr != g_idx.end() && g_itr->account == payer.name )
   {
      const governance_subscription_object& sub = *g_itr;
      const account_object* account_ptr = find_account( sub.governance_account );
      governance_subscriptions.insert( account_ptr );
      ++g_itr;
   }
   const account_object& registrar = get_account( payer.registrar );
   const account_object& referrer = get_account( payer.referrer );

   asset g_share = ( total_fees * GOVERNANCE_SHARE_PERCENT ) / PERCENT_100;
   asset registrar_share = ( total_fees * REFERRAL_SHARE_PERCENT ) / PERCENT_100;
   asset referrer_share = ( registrar_share * payer.referrer_rewards_percentage ) / PERCENT_100;
   registrar_share -= referrer_share;

   asset g_paid = pay_multi_fee_share( governance_subscriptions, g_share, true );
   asset registrar_paid = pay_fee_share( registrar, registrar_share, true );
   asset referrer_paid = pay_fee_share( referrer, referrer_share, true );

   total_fees -= ( g_paid + registrar_paid + referrer_paid );

   price credit_usd_price = get_liquidity_pool( SYMBOL_USD, SYMBOL_CREDIT ).hour_median_price;
   price usd_settlement_price = get_stablecoin_data( SYMBOL_USD ).settlement_price;
   price usd_market_price = get_liquidity_pool( SYMBOL_COIN, SYMBOL_USD ).base_hour_median_price( usd_settlement_price.base.symbol );

   if( usd_market_price < usd_settlement_price )       // If the market price of USD is below settlement price
   {
      asset usd_purchased = liquid_exchange( total_fees, SYMBOL_USD, true, false );      // Liquid Exchange into USD, without paying fees to avoid recursive fees. 

      create< asset_settlement_object >([&]( asset_settlement_object& fso ) 
      {
         fso.owner = NULL_ACCOUNT;
         fso.balance = usd_purchased;        // Settle USD purchased at below settlement price, to increase total Coin burned.
         fso.settlement_date = now + fc::minutes( 10 );
      });
   }
   else if( credit_usd_price < price(asset(1,SYMBOL_USD)/asset(1,SYMBOL_CREDIT)) )       // If price of credit is below $1.00 USD
   {
      liquid_exchange( total_fees, SYMBOL_CREDIT, true, false );       // Liquid Exchange into Credit asset, without paying fees to avoid recursive fees. 

      modify( props, [&]( dynamic_global_property_object& gpo )
      {
         gpo.accumulated_network_revenue += total_fees;
      });
   }
   else   // Remove Coin from Supply and increment network revenue. 
   {
      modify( props, [&]( dynamic_global_property_object& gpo )
      {
         gpo.accumulated_network_revenue += total_fees;
      });
   }

   return total_fees;

} FC_CAPTURE_AND_RETHROW() }


/** 
 * Pays protocol trading fees on taker orders.
 * 
 * taker: The account that is the taker on the trade
 * receives: The asset object being received from the trade
 * maker_int: The owner account of the interface of the maker of the trade
 * taker_int: The owner account of the interface of the taker of the trade
 */
asset database::pay_trading_fees( const account_object& taker, const asset& receives, 
   const account_name_type& maker_int, const account_name_type& taker_int )
{ try {
   asset total_fees = ( receives * TRADING_FEE_PERCENT ) / PERCENT_100;
   asset network_fee = ( total_fees * NETWORK_TRADING_FEE_PERCENT ) / PERCENT_100;
   asset maker_interface_share = ( total_fees * MAKER_TRADING_FEE_PERCENT ) / PERCENT_100;
   asset taker_interface_share = ( total_fees * TAKER_TRADING_FEE_PERCENT ) / PERCENT_100;
   asset maker_paid = asset( 0, receives.symbol );
   asset taker_paid = asset( 0, receives.symbol );

   if( maker_int.size() )
   {
      const account_object& m_int_acc = get_account( maker_int );
      const interface_object& m_interface = get_interface( maker_int );

      FC_ASSERT( m_int_acc.active && m_interface.active, 
         "Maker Interface: ${i} must be active",
         ("i",maker_int) );

      maker_paid = pay_fee_share( m_int_acc, maker_interface_share, true );
   }
   else
   {
      network_fee += maker_interface_share;
   }
   
   if( taker_int.size() )
   {
      const account_object& t_int_acc = get_account( taker_int );
      const interface_object& t_interface = get_interface( taker_int );

      FC_ASSERT( t_int_acc.active && t_interface.active, 
         "Taker Interface: ${i} must be active",
         ("i",taker_int) );

      taker_paid = pay_fee_share( t_int_acc, taker_interface_share, true );
   }
   else
   {
      network_fee += taker_interface_share;
   }
   
   pay_network_fees( taker, network_fee );

   asset total_paid = network_fee + maker_paid + taker_paid;

   ilog( "Account: ${a} paid trading fees: ${p}",
      ("a",taker.name)("p",total_paid.to_string()));
   return total_paid;
} FC_CAPTURE_AND_RETHROW() }


/**
 * Pays an advertising delivery operation to the provider
 * and pays a fee split to the demand side interface, the delivery provider
 * the bidder account, the audience members, and the network.
 */
asset database::pay_advertising_delivery( const account_object& provider, const account_object& demand, 
   const account_object& audience, const asset& value )
{ try {
   asset total_fees = ( value * ADVERTISING_FEE_PERCENT ) / PERCENT_100;
   
   asset demand_share = ( total_fees * DEMAND_ADVERTISING_FEE_PERCENT ) / PERCENT_100;
   asset audience_share = ( total_fees * AUDIENCE_ADVERTISING_FEE_PERCENT ) / PERCENT_100;
   asset network_fee = ( total_fees * NETWORK_ADVERTISING_FEE_PERCENT ) / PERCENT_100;

   asset demand_paid = pay_fee_share( demand, demand_share, true );
   asset audience_paid = pay_fee_share( audience, audience_share, true );
   pay_network_fees( provider, network_fee );

   asset fees_paid = network_fee + demand_paid + audience_paid;

   adjust_liquid_balance( provider.name, value - fees_paid );
   ilog( "Account: ${a} paid advertising delivery: ${v}",
      ("a",provider.name)("v",value.to_string()));

   return value;
} FC_CAPTURE_AND_RETHROW() }


/**
 * Pays the fees to a network contibutor, and splits fees to the account's governance 
 * account subscriptions, and registrar and referrer.
 */
asset database::pay_fee_share( const account_object& payee, const asset& amount, bool recursive )
{ try {
   asset total_fees = amount;

   if( recursive )
   {
      flat_set<const account_object*> governance_subscriptions;

      const auto& g_idx = get_index< governance_subscription_index >().indices().get< by_account_governance >();
      auto g_itr = g_idx.lower_bound( payee.name );

      while( g_itr != g_idx.end() && g_itr->account == payee.name )
      {
         const governance_subscription_object& sub = *g_itr;
         const account_object* account_ptr = find_account( sub.governance_account );
         governance_subscriptions.insert( account_ptr );
         ++g_itr;
      }
      const account_object& registrar = get_account( payee.registrar );
      const account_object& referrer = get_account( payee.referrer );

      asset g_share = ( amount * GOVERNANCE_SHARE_PERCENT ) / PERCENT_100;
      asset registrar_share = ( amount * REFERRAL_SHARE_PERCENT ) / PERCENT_100;
      asset referrer_share = ( registrar_share * payee.referrer_rewards_percentage ) / PERCENT_100;
      registrar_share -= referrer_share;

      asset g_paid = pay_multi_fee_share( governance_subscriptions, g_share, false );
      asset registrar_paid = pay_fee_share( registrar, registrar_share, false );
      asset referrer_paid = pay_fee_share( referrer, referrer_share, false );

      asset distribution = total_fees - ( g_paid + registrar_paid + referrer_paid );
      adjust_reward_balance( payee.name, distribution );
   }
   else
   {
      adjust_reward_balance( payee.name, total_fees );
   }
   
   return total_fees;

} FC_CAPTURE_AND_RETHROW() }


/**
 * Pays fees to a set of network contibutors, and splits fees to the account's governance 
 * account subscriptions, and registrar and referrer.
 */
asset database::pay_multi_fee_share( flat_set< const account_object* > payees, const asset& amount, bool recursive )
{ try {
   asset total_paid = asset( 0, amount.symbol );
   if( payees.size() )
   {
      asset fee_split = amount / payees.size();
      for( auto payee : payees )
      {
         total_paid += pay_fee_share( *payee, fee_split, recursive ); 
      }
   }
  
   return total_paid;

} FC_CAPTURE_AND_RETHROW() }


/**
 * Activates the delivery process for an ad bid.
 * 
 * Triggered by an operation broadcast from an audience member.
 * Rewards the Provider of the inventory, in addition to the 
 * audience member account that received the ad display.
 */
void database::deliver_ad_bid( const ad_bid_object& bid, const account_object& viewer )
{ try {
   const account_object& bidder = get_account( bid.bidder );
   const account_object& account = get_account( bid.account );
   const account_object& author = get_account( bid.author );
   const account_object& provider = get_account( bid.provider );

   const ad_campaign_object& campaign = get_ad_campaign( account.name, bid.campaign_id );
   const ad_inventory_object& inventory = get_ad_inventory( provider.name, bid.inventory_id );
   const ad_audience_object& audience = get_ad_audience( bidder.name, bid.audience_id );
   const ad_creative_object& creative = get_ad_creative( author.name, bid.creative_id );

   FC_ASSERT( campaign.budget >= bid.bid_price,
      "Campaign does not have sufficient budget to pay the delivery." );
   FC_ASSERT( !bid.is_delivered( viewer.name ) ,
      "Viewer has already been delivered by this bid." );

   const account_object& demand = get_account( campaign.interface );
   time_point now = head_block_time();

   if( campaign.active && 
      inventory.active && 
      audience.active && 
      creative.active && 
      now < bid.expiration &&
      now > campaign.begin &&
      now < campaign.end )
   {
      modify( campaign, [&]( ad_campaign_object& aco )
      {
         aco.budget -= bid.bid_price;
         aco.total_bids -= bid.bid_price;
         aco.last_updated = now;
      });

      modify( inventory, [&]( ad_inventory_object& aio )
      {
         aio.remaining--;
         aio.last_updated = now;
      });

      modify( bid, [&]( ad_bid_object& abo )
      {
         abo.remaining--;
         abo.delivered.insert( viewer.name );
         abo.last_updated = now;
      });

      pay_advertising_delivery( provider, demand, viewer, bid.bid_price );

      ilog( "Delivered Ad Bid to audience Account: ${v} Bid: ${b}",
         ("v",viewer.name)("b",bid.bid_id));

      if( bid.remaining == 0 )
      {
         ilog( "Removed: ${v}",("v",bid));
         remove( bid );
      }
      if( inventory.remaining == 0 )
      {
         ilog( "Removed: ${v}",("v",inventory));
         remove( inventory );
      }
      if( campaign.budget.amount == 0 )
      {
         ilog( "Removed: ${v}",("v",campaign));
         remove( campaign );
      }
   }
} FC_CAPTURE_AND_RETHROW() }


/**
 * Updates the ad campaign of a bidder, and removes an ad bid object.
 */
void database::cancel_ad_bid( const ad_bid_object& bid )
{ try {
   uint32_t prev_remaining = bid.remaining;
   asset prev_price = bid.bid_price;
   asset bid_total_remaining = prev_remaining * prev_price;
   const ad_campaign_object& campaign = get_ad_campaign( bid.account, bid.campaign_id );

   modify( campaign, [&]( ad_campaign_object& aco )
   {
      aco.total_bids -= bid_total_remaining;
   });

   ilog( "Removed: ${v}",("v",bid));
   remove( bid );
} FC_CAPTURE_AND_RETHROW() }


/**
 * Updates the ad campaign of a bidder, and removes an ad bid object.
 */
void database::cancel_community_enterprise( const community_enterprise_object& e )
{ try {
   const reward_fund_object& reward_fund = get_reward_fund( e.daily_budget.symbol );
   auto& approval_idx = get_index<enterprise_approval_index>().indices().get<by_enterprise_id>();
   auto approval_itr = approval_idx.lower_bound( boost::make_tuple( e.creator, e.enterprise_id ) );

   while( approval_itr != approval_idx.end() && 
      approval_itr->creator == e.creator && 
      approval_itr->enterprise_id == e.enterprise_id )
   {
      const enterprise_approval_object& old_approval = *approval_itr;
      ++approval_itr;

      ilog( "Removed: ${v}",("v",old_approval));
      remove( old_approval );
   }

   asset pending = e.pending_budget;
   modify( reward_fund, [&]( reward_fund_object& o )
   {
      o.adjust_community_fund_balance( pending );    // Return pending budget to the community fund.
   });

   ilog( "Removed: ${v}",("v",e.enterprise_id));
   remove( e );
} FC_CAPTURE_AND_RETHROW() }


void database::init_hardforks()
{
   _hardfork_times[ 0 ] = fc::time_point( GENESIS_TIME );
   _hardfork_versions[ 0 ] = hardfork_version( 0, 0 );
   // FC_ASSERT( HARDFORK_0_1 == 1, "Invalid hardfork configuration" );
   // _hardfork_times[ HARDFORK_0_1 ] = fc::time_point( HARDFORK_0_1_TIME );
   // _hardfork_versions[ HARDFORK_0_1 ] = HARDFORK_0_1_VERSION;

   const auto& hardforks = get_hardfork_property_object();
   FC_ASSERT( hardforks.last_hardfork <= NUM_HARDFORKS,
      "Chain knows of more hardforks than configuration.", ("hardforks.last_hardfork",hardforks.last_hardfork)("NUM_HARDFORKS",NUM_HARDFORKS) );
   FC_ASSERT( _hardfork_versions[ hardforks.last_hardfork ] <= BLOCKCHAIN_VERSION,
      "Blockchain version is older than last applied hardfork." );
   FC_ASSERT( BLOCKCHAIN_HARDFORK_VERSION == _hardfork_versions[ NUM_HARDFORKS ] );
}


/**
 * Expire all orders that have exceeded their expiration time.
 */
void database::clear_expired_operations()
{ try {
   time_point now = head_block_time();

   // ilog( "Clear Expired Operations:", ("now", now) );
   
   const auto& connection_req_index = get_index< connection_request_index >().indices().get< by_expiration >();
   while( !connection_req_index.empty() && connection_req_index.begin()->expiration <= now )
   {
      const connection_request_object& req = *connection_req_index.begin();
      ilog( "Removed: ${v}",("v",req));
      remove( req );
   }

   const auto& limit_index = get_index< limit_order_index >().indices().get< by_expiration >();
   while( !limit_index.empty() && limit_index.begin()->expiration <= now )
   {
      const limit_order_object& order = *limit_index.begin();
      cancel_limit_order( order );
   }

   const auto& margin_index = get_index< margin_order_index >().indices().get< by_expiration >();
   while( !margin_index.empty() && margin_index.begin()->expiration <= now )
   {
      const margin_order_object& order = *margin_index.begin();
      close_margin_order( order );
   }

   const auto& transfer_req_index = get_index< transfer_request_index >().indices().get< by_expiration >();
   while( !transfer_req_index.empty() && transfer_req_index.begin()->expiration <= now )
   {
      const transfer_request_object& req = *transfer_req_index.begin();
      ilog( "Removed: ${v}",("v",req));
      remove( req );
   }

   const auto& transfer_rec_index = get_index< transfer_recurring_request_index >().indices().get< by_expiration >();
   while( !transfer_rec_index.empty() && transfer_rec_index.begin()->expiration <= now )
   {
      const transfer_recurring_request_object& rec = *transfer_rec_index.begin();
      ilog( "Removed: ${v}",("v",rec));
      remove( rec );
   }

   const auto& account_member_request_idx = get_index< account_member_request_index >().indices().get< by_expiration >();
   while( !account_member_request_idx.empty() && account_member_request_idx.begin()->expiration <= now )
   {
      const account_member_request_object& req = *account_member_request_idx.begin();
      ilog( "Removed: ${v}",("v",req));
      remove( req );
   }

   const auto& account_member_invite_idx = get_index< account_member_invite_index >().indices().get< by_expiration >();
   while( !account_member_invite_idx.empty() && account_member_invite_idx.begin()->expiration <= now )
   {
      const account_member_invite_object& inv = *account_member_invite_idx.begin();
      ilog( "Removed: ${v}",("v",inv));
      remove( inv );
   }

   const auto& community_join_request_idx = get_index< community_join_request_index >().indices().get< by_expiration >();
   while( !community_join_request_idx.empty() && community_join_request_idx.begin()->expiration <= now )
   {
      const community_join_request_object& req = *community_join_request_idx.begin();
      ilog( "Removed: ${v}",("v",req));
      remove( req );
   }

   const auto& community_join_invite_idx = get_index< community_join_invite_index >().indices().get< by_expiration >();
   while( !community_join_invite_idx.empty() && community_join_invite_idx.begin()->expiration <= now )
   {
      const community_join_invite_object& inv = *community_join_invite_idx.begin();
      ilog( "Removed: ${v}",("v",inv));
      remove( inv );
   }

   const auto& enterprise_idx = get_index< community_enterprise_index >().indices().get< by_expiration >();
   while( !enterprise_idx.empty() && enterprise_idx.begin()->expiration <= now )
   {
      const community_enterprise_object& ent = *enterprise_idx.begin();
      cancel_community_enterprise( ent );
   }

   const auto& bid_index = get_index< ad_bid_index >().indices().get< by_expiration >();
   while( !bid_index.empty() && bid_index.begin()->expiration <= now )
   {
      const ad_bid_object& bid = *bid_index.begin();
      cancel_ad_bid( bid );
   }

   // Process expired force settlement orders
   const auto& settlement_index = get_index< asset_settlement_index >().indices().get< by_expiration >();
   if( !settlement_index.empty() )
   {
      asset_symbol_type current_asset = settlement_index.begin()->settlement_asset_symbol();
      asset max_settlement_volume;
      price settlement_fill_price;
      price settlement_price;
      bool current_asset_finished = false;
      bool extra_dump = false;

      auto next_asset = [&current_asset, &current_asset_finished, &settlement_index, &extra_dump] {
         auto bound = settlement_index.upper_bound(current_asset);
         if( bound == settlement_index.end() )
         {
            if( extra_dump )
            {
               ilog( "next_asset() returning false" );
            }
            return false;
         }
         if( extra_dump )
         {
            ilog( "next_asset returning true, bound is ${b}", ("b", *bound) );
         }
         current_asset = bound->settlement_asset_symbol();
         current_asset_finished = false;
         return true;
      };

      uint32_t count = 0;

      // At each iteration, we either consume the current order and remove it, or we move to the next asset
      for( auto itr = settlement_index.lower_bound( current_asset );
         itr != settlement_index.end();
         itr = settlement_index.lower_bound( current_asset ) )
      {
         ++count;
         const asset_settlement_object& order = *itr;
         auto order_id = order.id;
         current_asset = order.settlement_asset_symbol();
         const asset_object& mia_object = get_asset( current_asset );
         const asset_stablecoin_data_object& mia_stablecoin = get_stablecoin_data( mia_object.symbol );

         extra_dump = ( (count >= 1000) && (count <= 1020) );

         if( extra_dump )
         {
            wlog( "clear_expired_operations() dumping extra data for iteration ${c}", ("c", count) );
            ilog( "head_block_num is ${hb} current_asset is ${a}", ("hb", head_block_num())("a", current_asset) );
         }

         if( mia_stablecoin.has_settlement() )
         {
            ilog( "Canceling a force settlement because of black swan" );
            cancel_settle_order( order );
            continue;
         }

         // Has this order not reached its settlement date?
         if( order.settlement_date > now )
         {
            if( next_asset() )
            {
               if( extra_dump )
               {
                  ilog( "next_asset() returned true when order.settlement_date > head_block_time()" );
               }
               continue;
            }
            break;
         }
         
         if( mia_stablecoin.current_feed.settlement_price.is_null() )
         {
            ilog("Canceling a force settlement in ${asset} because settlement price is null",
                 ("asset", mia_object.symbol));

            cancel_settle_order( order );
            continue;
         }
         
         if( max_settlement_volume.symbol != current_asset ) // only calculate once per asset
         {  
            const asset_dynamic_data_object& dyn_data = get_dynamic_data( mia_object.symbol );
            max_settlement_volume = asset( mia_stablecoin.max_asset_settlement_volume(dyn_data.get_total_supply().amount), mia_object.symbol );
         }

         if( mia_stablecoin.force_settled_volume >= max_settlement_volume.amount || current_asset_finished )
         {
            if( next_asset() )
            {
               if( extra_dump )
               {
                  ilog( "next_asset() returned true when mia.force_settled_volume >= max_settlement_volume.amount" );
               }
               continue;
            }
            break;
         }

         if( settlement_fill_price.base.symbol != current_asset )  // only calculate once per asset
         {
            uint16_t offset = mia_stablecoin.asset_settlement_offset_percent;
            settlement_fill_price = mia_stablecoin.current_feed.settlement_price / ratio_type( PERCENT_100 - offset, PERCENT_100 );
         }
            
         if( settlement_price.base.symbol != current_asset )  // only calculate once per asset
         {
            settlement_price = settlement_fill_price;
         }

         auto& call_index = get_index< call_order_index >().indices().get< by_collateral >();
         asset settled = asset( mia_stablecoin.force_settled_volume , mia_object.symbol);
         // Match against the least collateralized short until the settlement is finished or we reach max settlements
         while( settled < max_settlement_volume && find(order_id) )
         {
            auto itr = call_index.lower_bound( boost::make_tuple( price::min( mia_stablecoin.backing_asset, mia_object.symbol )));
            // There should always be a call order, since asset exists
            FC_ASSERT( itr != call_index.end() && 
               itr->debt_type() == mia_object.symbol, 
               "Call order asset must be the same as market issued asset." );
            asset max_settlement = max_settlement_volume - settled;

            if( order.balance.amount == 0 )
            {
               wlog( "0 settlement detected" );
               cancel_settle_order( order );
               break;
            }
            asset new_settled = match(*itr, order, settlement_price, max_settlement, settlement_fill_price);
            if( new_settled.amount == 0 ) // unable to fill this settle order
            {
               if( find( order_id ) ) // the settle order hasn't been cancelled
                  current_asset_finished = true;
               break;
            }
            settled += new_settled;
         }
         if( mia_stablecoin.force_settled_volume != settled.amount )
         {
            modify(mia_stablecoin, [settled](asset_stablecoin_data_object& b) 
            {
               b.force_settled_volume = settled.amount;
            });
         }
      }
   }
} FC_CAPTURE_AND_RETHROW() }

void database::process_hardforks()
{
   try
   {
      // If there are upcoming hardforks and the next one is later, do nothing
      const auto& hardforks = get_hardfork_property_object();

      while( _hardfork_versions[ hardforks.last_hardfork ] < hardforks.next_hardfork
         && hardforks.next_hardfork_time <= head_block_time() )
      {
         if( hardforks.last_hardfork < NUM_HARDFORKS ) 
         {
            apply_hardfork( hardforks.last_hardfork + 1 );
         }
         else
         {
            throw unknown_hardfork_exception();
         }
      }

      // ilog( "Processed Hardforks" );
   }
   FC_CAPTURE_AND_RETHROW()
}

bool database::has_hardfork( uint32_t hardfork )const
{
   return get_hardfork_property_object().processed_hardforks.size() > hardfork;
}

void database::set_hardfork( uint32_t hardfork, bool apply_now )
{
   auto const& hardforks = get_hardfork_property_object();

   for( uint32_t i = hardforks.last_hardfork + 1; i <= hardfork && i <= NUM_HARDFORKS; i++ )
   {
      modify( hardforks, [&]( hardfork_property_object& hpo )
      {
         hpo.next_hardfork = _hardfork_versions[i];
         hpo.next_hardfork_time = head_block_time();
      } );

      if( apply_now )
         apply_hardfork( i );
   }
}

void database::apply_hardfork( uint32_t hardfork )
{
   if( _log_hardforks )
      elog( "HARDFORK ${hf} at block ${b}", ("hf", hardfork)("b", head_block_num()) );

   switch( hardfork )
   {
      case HARDFORK_0_1:
         break;
      
   }

   modify( get_hardfork_property_object(), [&]( hardfork_property_object& hfp )
   {
      FC_ASSERT( hardfork == hfp.last_hardfork + 1, "Hardfork being applied out of order", ("hardfork",hardfork)("hfp.last_hardfork",hfp.last_hardfork) );
      FC_ASSERT( hfp.processed_hardforks.size() == hardfork, "Hardfork being applied out of order" );
      hfp.processed_hardforks.push_back( _hardfork_times[ hardfork ] );
      hfp.last_hardfork = hardfork;
      hfp.current_hardfork_version = _hardfork_versions[ hardfork ];
      FC_ASSERT( hfp.processed_hardforks[ hfp.last_hardfork ] == _hardfork_times[ hfp.last_hardfork ], "Hardfork processing failed sanity check..." );
   } );

   push_virtual_operation( hardfork_operation( hardfork ), true );
}

/**
 * Verifies all supply invariants.
 */
void database::validate_invariants()const
{ try {
   // ilog( "Validate Invariants" );
   const auto& asset_idx = get_index< asset_dynamic_data_index >().indices().get< by_symbol >();
   const auto& balance_idx = get_index< account_balance_index >().indices().get< by_symbol >();
   
   auto asset_itr = asset_idx.begin();
   while( asset_itr != asset_idx.end() )
   {
      const asset_dynamic_data_object& addo = *asset_itr;
      asset total_account_balance_supply =  addo.get_account_balance_supply();
      
      FC_ASSERT( addo.get_delegated_supply() == addo.get_receiving_supply(),
         "Asset Supply error: Delegated supply not equal to receiving supply",
         ("Asset", addo.symbol ) );

      asset total_account_balances = asset( 0, addo.symbol );

      auto balance_itr = balance_idx.lower_bound( addo.symbol );

      while( balance_itr != balance_idx.end() && 
         balance_itr->symbol == addo.symbol )
      {
         const account_balance_object& abo = *balance_itr;
         total_account_balances += abo.get_total_balance();
         ++balance_itr;
      }

      FC_ASSERT( total_account_balances == total_account_balance_supply,
            "Account Balance Error: Balance of asset ${s} account balance sum: ${b} not equal to total account balance supply: ${t}.", 
            ("s", addo.symbol)("b", total_account_balances)("t", total_account_balance_supply) );

      ++asset_itr;
   }

} FC_CAPTURE_LOG_AND_RETHROW( (head_block_num()) ) }


} } // node::chain