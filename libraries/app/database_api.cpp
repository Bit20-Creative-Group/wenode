#include <node/app/api_context.hpp>
#include <node/app/application.hpp>
#include <node/app/database_api.hpp>

#include <node/protocol/get_config.hpp>

#include <node/chain/util/reward.hpp>

#include <fc/bloom_filter.hpp>
#include <fc/smart_ref_impl.hpp>
#include <fc/crypto/hex.hpp>

#include <boost/range/iterator_range.hpp>
#include <boost/algorithm/string.hpp>


#include <cctype>

#include <cfenv>
#include <iostream>

#define GET_REQUIRED_FEES_MAX_RECURSION 4

namespace node { namespace app {

class database_api_impl;

applied_operation::applied_operation() {}

applied_operation::applied_operation( const operation_object& op_obj ): 
   trx_id( op_obj.trx_id ),
   block( op_obj.block ),
   trx_in_block( op_obj.trx_in_block ),
   op_in_trx( op_obj.op_in_trx ),
   virtual_op( op_obj.virtual_op ),
   timestamp( op_obj.timestamp )
{
   //fc::raw::unpack( op_obj.serialized_op, op );     // g++ refuses to compile this as ambiguous
   op = fc::raw::unpack< operation >( op_obj.serialized_op );
}

void find_accounts( set< string >& accounts, const discussion& d ) 
{
   accounts.insert( d.author );
}

class database_api_impl : public std::enable_shared_from_this<database_api_impl>
{
   public:
      database_api_impl( const node::app::api_context& ctx  );
      ~database_api_impl();


      //=================//
      // === Globals === //
      //=================//


      fc::variant_object                              get_config()const;

      dynamic_global_property_api_obj                 get_dynamic_global_properties()const;

      median_chain_property_api_obj                   get_median_chain_properties()const;

      producer_schedule_api_obj                       get_producer_schedule()const;

      hardfork_version                                get_hardfork_version()const;

      scheduled_hardfork                              get_next_scheduled_hardfork()const;


      //===================//
      // === Accounts ==== //
      //===================//


      vector< account_api_obj >                       get_accounts( vector< string > names )const;

      vector< account_api_obj >                       get_accounts_by_followers( string from, uint32_t limit )const;
      
      vector< account_concise_api_obj >               get_concise_accounts( vector< string > names )const;

      vector< extended_account >                      get_full_accounts( vector< string > names )const;

      map< uint32_t, applied_operation >              get_account_history( string account, uint64_t from, uint32_t limit )const;

      vector< message_state >                         get_messages( vector< string > names )const;

      list_state                                      get_list( string name, string list_id )const;

      vector< account_list_state >                    get_account_lists( vector< string > names )const;

      poll_state                                      get_poll( string name, string poll_id )const;

      vector< account_poll_state >                    get_account_polls( vector< string > names )const;

      vector< balance_state >                         get_balances( vector< string > names )const;

      vector< confidential_balance_api_obj >          get_confidential_balances( const confidential_query& query )const;

      vector< key_state >                             get_keychains( vector< string > names )const;

      set< string >                                   lookup_accounts( string lower_bound_name, uint32_t limit )const;

      uint64_t                                        get_account_count()const;


      //================//
      // === Assets === //
      //================//


      vector< extended_asset >                        get_assets( vector< string > assets )const;

      optional< escrow_api_obj >                      get_escrow( string from, string escrow_id )const;


      //=====================//
      // === Communities === //
      //=====================//


      vector< extended_community >                    get_communities( vector< string > communities )const;

      vector< extended_community >                    get_communities_by_subscribers( string from, uint32_t limit )const;
      

      //=================//
      // === Network === //
      //=================//


      vector< account_network_state >                 get_account_network_state( vector< string > names )const;

      vector< account_name_type >                     get_active_producers()const;

      vector< producer_api_obj >                      get_producers_by_voting_power( string from, uint32_t limit )const;

      vector< producer_api_obj >                      get_producers_by_mining_power( string from, uint32_t limit )const;

      vector< network_officer_api_obj >               get_development_officers_by_voting_power( string currency, string from, uint32_t limit )const;

      vector< network_officer_api_obj >               get_marketing_officers_by_voting_power( string currency, string from, uint32_t limit )const;

      vector< network_officer_api_obj >               get_advocacy_officers_by_voting_power( string currency, string from, uint32_t limit )const;

      vector< executive_board_api_obj >               get_executive_boards_by_voting_power( string from, uint32_t limit )const;

      vector< supernode_api_obj >                     get_supernodes_by_view_weight( string from, uint32_t limit )const;

      vector< interface_api_obj >                     get_interfaces_by_users( string from, uint32_t limit )const;

      vector< governance_account_api_obj >            get_governance_accounts_by_subscriber_power( string from, uint32_t limit )const;

      vector< enterprise_api_obj >                    get_enterprise_by_voting_power( string from, string from_id, uint32_t limit )const;


      //================//
      // === Market === //
      //================//


      vector< order_state >                           get_open_orders( vector< string > names )const;

      market_limit_orders                             get_limit_orders( string buy_symbol, string sell_symbol, uint32_t limit )const;

      market_margin_orders                            get_margin_orders( string buy_symbol, string sell_symbol, uint32_t limit )const;

      market_option_orders                            get_option_orders( string buy_symbol, string sell_symbol, uint32_t limit )const;
      
      market_call_orders                              get_call_orders( string buy_symbol, string sell_symbol, uint32_t limit )const;

      market_auction_orders                           get_auction_orders( string buy_symbol, string sell_symbol, uint32_t limit )const;
      
      market_credit_loans                             get_credit_loans( string buy_symbol, string sell_symbol, uint32_t limit )const;

      vector< credit_pool_api_obj >                   get_credit_pools( vector< string > assets )const;

      vector< liquidity_pool_api_obj >                get_liquidity_pools( string buy_symbol, string sell_symbol )const;

      vector< option_pool_api_obj >                   get_option_pools( string buy_symbol, string sell_symbol )const;

      market_state                                    get_market_state( string buy_symbol, string sell_symbol )const;


      //=============//
      // === Ads === //
      //=============//


      vector< account_ad_state >                      get_account_ads( vector< string > names )const;

      vector< ad_bid_state >                          get_interface_audience_bids( const ad_query& query )const;


      //==================//
      // === Products === //
      //==================//


      product_sale_api_obj                            get_product_sale( string seller, string product_id )const;

      product_auction_sale_api_obj                    get_product_auction_sale( string seller, string auction_id )const;

      vector< account_product_state >                 get_account_products( vector< string > names )const;


      //=====================//
      // === Graph Data  === //
      //=====================//


      graph_data_state                                get_graph_query( const graph_query& query )const;

      vector< graph_node_property_api_obj >           get_graph_node_properties( vector< string > names )const;

      vector< graph_edge_property_api_obj >           get_graph_edge_properties( vector< string > names )const;


      //================//
      // === Search === //
      //================//


      search_result_state                             get_search_query( const search_query& query )const;


      //=================================//
      // === Blocks and transactions === //
      //=================================//


      optional< block_header >                        get_block_header( uint64_t block_num )const;

      optional< signed_block_api_obj >                get_block( uint64_t block_num )const;

      vector< applied_operation >                     get_ops_in_block( uint64_t block_num, bool only_virtual )const;

      std::string                                     get_transaction_hex( const signed_transaction& trx)const;

      annotated_signed_transaction                    get_transaction( transaction_id_type trx_id )const;

      set< public_key_type >                          get_required_signatures( const signed_transaction& trx, const flat_set< public_key_type >& available_keys )const;

      set< public_key_type >                          get_potential_signatures( const signed_transaction& trx )const;

      bool                                            verify_authority( const signed_transaction& trx )const;

      bool                                            verify_account_authority( const string& name_or_id, const flat_set< public_key_type >& signers )const;


      //======================//
      // === Posts + Tags === //
      //======================//

      comment_interaction_state                       get_comment_interactions( string author, string permlink )const;

      vector< account_vote >                          get_account_votes( string account, string from_author, string from_permlink, uint32_t limit )const;

      vector< account_view >                          get_account_views( string account, string from_author, string from_permlink, uint32_t limit )const;

      vector< account_share >                         get_account_shares( string account, string from_author, string from_permlink, uint32_t limit )const;

      vector< account_moderation >                    get_account_moderation( string account, string from_author, string from_permlink, uint32_t limit )const;

      vector< account_tag_following_api_obj >         get_account_tag_followings( vector< string > tags )const;

      vector< tag_api_obj >                           get_top_tags( string after_tag, uint32_t limit )const;

      vector< pair< tag_name_type, uint32_t > >       get_tags_used_by_author( string author )const;


      //=====================//
      // === Discussions === //
      //=====================//


      discussion                                      get_content( string author, string permlink )const;
      
      vector< discussion >                            get_content_replies( string parent, string parent_permlink )const;

      vector< discussion >                            get_replies_by_last_update( account_name_type start_author, string start_permlink, uint32_t limit )const;

      vector< discussion >                            get_discussions_by_sort_rank( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_feed( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_blog( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_featured( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_recommended( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_comments( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_payout(const discussion_query& query )const;

      vector< discussion >                            get_post_discussions_by_payout( const discussion_query& query )const;

      vector< discussion >                            get_comment_discussions_by_payout( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_created( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_active( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_votes( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_views( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_shares( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_children( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_vote_power( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_view_power( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_share_power( const discussion_query& query )const;

      vector< discussion >                            get_discussions_by_comment_power( const discussion_query& query )const;


      //===============//
      // === State === //
      //===============//


      state                                           get_state( string path )const;


      //=======================//
      // === Subscriptions === //
      //=======================//


      void                                            set_block_applied_callback( std::function<void( const variant& block_id )> cb );


      //=========================//
      // === Signal Handlers === //
      //=========================//


      void                                             on_applied_block( const chain::signed_block& b );

      std::function<void( const variant&)>             _block_applied_callback;

      node::chain::database&                          _db;

      boost::signals2::scoped_connection              _block_applied_connection;

      bool                                            _disable_get_block = false;

      discussion                                      get_discussion( comment_id_type id, uint32_t truncate_body )const;

      static bool filter_default( const comment_api_obj& c ) { return false; }
      static bool exit_default( const comment_api_obj& c )   { return false; }
      static bool tag_exit_default( const tags::tag_object& c ) { return false; }

      template< typename Index, typename StartItr >
      vector< discussion > get_discussions( 
         const discussion_query& q,
         const string& community,
         const string& tag,
         comment_id_type parent,
         const Index& idx,
         StartItr itr,
         uint32_t truncate_body,
         const std::function< bool( const comment_api_obj& ) >& filter,
         const std::function< bool( const comment_api_obj& ) >& exit,
         const std::function< bool( const tags::tag_object& ) >& tag_exit,
         bool ignore_parent
         )const;
         
      comment_id_type get_parent( const discussion_query& q )const;

      void recursively_fetch_content( state& _state, discussion& root, set<string>& referenced_accounts )const;
};


   //======================//
   // === Constructors === //
   //======================//


database_api::database_api( const node::app::api_context& ctx )
   : my( new database_api_impl( ctx ) ) {}

database_api::~database_api() {}

database_api_impl::database_api_impl( const node::app::api_context& ctx )
   : _db( *ctx.app.chain_database() )
{
   wlog( "creating database api ${x}", ("x",int64_t(this)) );

   _disable_get_block = ctx.app._disable_get_block;
}

database_api_impl::~database_api_impl()
{
   elog( "freeing database api ${x}", ("x",int64_t(this)) );
}

void database_api::on_api_startup() {}

uint256_t to256( const fc::uint128& t )
{
   uint256_t results( t.high_bits() );
   results <<= 65;
   results += t.low_bits();
   return results;
}



   //=================//
   // === Globals === //
   //=================//
   

fc::variant_object database_api::get_config()const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_config();
   });
}

fc::variant_object database_api_impl::get_config()const
{
   return node::protocol::get_config();
}

dynamic_global_property_api_obj database_api::get_dynamic_global_properties()const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_dynamic_global_properties();
   });
}

dynamic_global_property_api_obj database_api_impl::get_dynamic_global_properties()const
{
   return dynamic_global_property_api_obj( _db.get( dynamic_global_property_id_type() ), _db );
}

median_chain_property_api_obj database_api::get_median_chain_properties()const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_median_chain_properties();
   });
}

median_chain_property_api_obj database_api_impl::get_median_chain_properties()const
{
   return median_chain_property_api_obj( _db.get_median_chain_properties() );
}

producer_schedule_api_obj database_api::get_producer_schedule()const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_producer_schedule();
   });
}

producer_schedule_api_obj database_api_impl::get_producer_schedule()const
{
   return producer_schedule_api_obj( _db.get_producer_schedule() );
}

hardfork_version database_api::get_hardfork_version()const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_hardfork_version();
   });
}

hardfork_version database_api_impl::get_hardfork_version()const
{
   const hardfork_property_object& hpo = _db.get( hardfork_property_id_type() );
   return hpo.current_hardfork_version;
}

scheduled_hardfork database_api::get_next_scheduled_hardfork() const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_next_scheduled_hardfork();
   });
}

scheduled_hardfork database_api_impl::get_next_scheduled_hardfork() const
{
   scheduled_hardfork shf;
   const hardfork_property_object& hpo = _db.get( hardfork_property_id_type() );
   shf.hf_version = hpo.next_hardfork;
   shf.live_time = hpo.next_hardfork_time;
   return shf;
}


   //===================//
   // === Accounts ==== //
   //===================//


vector< account_api_obj > database_api::get_accounts( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_accounts( names );
   });
}

vector< account_api_obj > database_api_impl::get_accounts( vector< string > names )const
{
   const auto& account_idx  = _db.get_index< account_index >().indices().get< by_name >();

   vector< account_api_obj > results;

   for( auto name: names )
   {
      auto account_itr = account_idx.find( name );
      if( account_itr != account_idx.end() )
      {
         results.push_back( account_api_obj( *account_itr, _db ) );
      }  
   }

   return results;
}


vector< account_api_obj > database_api::get_accounts_by_followers( string from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_accounts_by_followers( from, limit );
   });
}

vector< account_api_obj > database_api_impl::get_accounts_by_followers( string from, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 1000 ) );
   vector< account_api_obj > results;
   results.reserve( limit );

   const auto& account_idx  = _db.get_index< account_index >().indices().get< by_follower_count >();
   const auto& name_idx  = _db.get_index< account_index >().indices().get< by_name >();

   auto account_itr = account_idx.begin();
  
   if( from.size() )
   {
      auto name_itr = name_idx.find( from );
      FC_ASSERT( name_itr != name_idx.end(),
         "Invalid Community name ${n}", ("n",from) );
      account_itr = account_idx.iterator_to( *name_itr );
   }

   while( account_itr != account_idx.end() && results.size() < limit )
   {
      results.push_back( account_api_obj( *account_itr, _db ) );
      ++account_itr;
   }
   return results;
}

vector< account_concise_api_obj > database_api::get_concise_accounts( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_concise_accounts( names );
   });
}

vector< account_concise_api_obj > database_api_impl::get_concise_accounts( vector< string > names )const
{
   const auto& account_idx  = _db.get_index< account_index >().indices().get< by_name >();

   vector< account_concise_api_obj > results;

   for( auto name: names )
   {
      auto account_itr = account_idx.find( name );
      if ( account_itr != account_idx.end() )
      {
         results.push_back( *account_itr );
      }  
   }

   return results;
}

vector< extended_account > database_api::get_full_accounts( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_full_accounts( names );
   });
}

/**
 * Retrieves all relevant state information regarding a list of specified accounts, including:
 * - Balances, active transfers and requests.
 * - Business Account details, invites and requests.
 * - Connection Details and requests.
 * - Incoming and Outgoing messages, and conversations with accounts.
 * - Community Moderation and ownership details, and invites and requests.
 * - Producer, network officer, and executive board details and votes.
 * - Interface and Supernode details.
 * - Advertising Campaigns, inventory, creative, bids, and audiences.
 */
vector< extended_account > database_api_impl::get_full_accounts( vector< string > names )const
{
   const auto& account_idx  = _db.get_index< account_index >().indices().get< by_name >();
   const auto& balance_idx = _db.get_index< account_balance_index >().indices().get< by_owner >();

   const auto& verified_verifier_idx = _db.get_index< account_verification_index >().indices().get< by_verified_verifier >();
   const auto& verifier_verified_idx = _db.get_index< account_verification_index >().indices().get< by_verifier_verified >();

   const auto& business_idx = _db.get_index< account_business_index >().indices().get< by_account >();
   const auto& bus_key_idx = _db.get_index< account_member_key_index >().indices().get< by_member_business >();
   const auto& community_key_idx = _db.get_index< community_member_key_index >().indices().get< by_member_community >();
   const auto& following_idx = _db.get_index< account_following_index >().indices().get< by_account >();
   const auto& connection_a_idx = _db.get_index< account_connection_index >().indices().get< by_account_a >();
   const auto& connection_b_idx = _db.get_index< account_connection_index >().indices().get< by_account_b >();
   const auto& inbox_idx = _db.get_index< message_index >().indices().get< by_account_inbox >();
   const auto& outbox_idx = _db.get_index< message_index >().indices().get< by_account_outbox >();

   const auto& limit_idx = _db.get_index< limit_order_index >().indices().get< by_account >();
   const auto& margin_idx = _db.get_index< margin_order_index >().indices().get< by_account >();
   const auto& call_idx = _db.get_index< call_order_index >().indices().get< by_account >();
   const auto& loan_idx = _db.get_index< credit_loan_index >().indices().get< by_owner >();
   const auto& collateral_idx = _db.get_index< credit_collateral_index >().indices().get< by_owner >();
   const auto& moderator_idx = _db.get_index< community_moderator_vote_index >().indices().get< by_account_community_rank >();

   const auto& connection_req_idx = _db.get_index< account_connection_request_index >().indices().get< by_req_account >();
   const auto& connection_acc_idx = _db.get_index< account_connection_request_index >().indices().get< by_account_req >();

   const auto& account_req_idx = _db.get_index< account_member_request_index >().indices().get< by_account_business >();
   const auto& bus_req_idx = _db.get_index< account_member_request_index >().indices().get< by_business_account >();
   const auto& account_inv_idx = _db.get_index< account_member_invite_index >().indices().get< by_account >();
   const auto& member_inv_idx = _db.get_index< account_member_invite_index >().indices().get< by_member >();
   const auto& bus_inv_idx = _db.get_index< account_member_invite_index >().indices().get< by_business >();

   const auto& incoming_account_officer_idx = _db.get_index< account_officer_vote_index >().indices().get< by_officer >();
   const auto& incoming_account_exec_idx = _db.get_index< account_executive_vote_index >().indices().get< by_executive >();
   const auto& incoming_business_officer_idx = _db.get_index< account_officer_vote_index >().indices().get< by_business_account_rank >();
   const auto& incoming_business_exec_idx = _db.get_index< account_executive_vote_index >().indices().get< by_business_account_role_rank >();
   const auto& outgoing_account_officer_idx = _db.get_index< account_officer_vote_index >().indices().get< by_account_business_rank >();
   const auto& outgoing_account_exec_idx = _db.get_index< account_executive_vote_index >().indices().get< by_account_business_role_rank >();

   const auto& community_req_idx = _db.get_index< community_join_request_index >().indices().get< by_account_community >();
   const auto& community_acc_inv_idx = _db.get_index< community_join_invite_index >().indices().get< by_account >();
   const auto& community_member_inv_idx = _db.get_index< community_join_invite_index >().indices().get< by_member >();
   const auto& community_member_idx = _db.get_index< community_member_index >().indices().get< by_name >();

   const auto& transfer_req_idx = _db.get_index< transfer_request_index >().indices().get< by_request_id >();
   const auto& transfer_from_req_idx = _db.get_index< transfer_request_index >().indices().get< by_from_account >();
   const auto& recurring_idx = _db.get_index< transfer_recurring_index >().indices().get< by_transfer_id >();
   const auto& recurring_to_idx = _db.get_index< transfer_recurring_index >().indices().get< by_to_account >();
   const auto& recurring_req_idx = _db.get_index< transfer_recurring_request_index >().indices().get< by_request_id >();
   const auto& recurring_from_req_idx = _db.get_index< transfer_recurring_request_index >().indices().get< by_from_account >();

   const auto& producer_idx = _db.get_index< producer_index >().indices().get< by_name >();
   const auto& executive_idx = _db.get_index< executive_board_index >().indices().get< by_account >();
   const auto& officer_idx = _db.get_index< network_officer_index >().indices().get< by_account >();
   const auto& enterprise_idx = _db.get_index< enterprise_index >().indices().get< by_account >();
   const auto& interface_idx = _db.get_index< interface_index >().indices().get< by_account >();
   const auto& supernode_idx = _db.get_index< supernode_index >().indices().get< by_account >();
   const auto& governance_idx = _db.get_index< governance_account_index >().indices().get< by_account >();
   const auto& validation_idx = _db.get_index< block_validation_index >().indices().get< by_producer_height >();
   
   const auto& incoming_producer_vote_idx = _db.get_index< producer_vote_index >().indices().get< by_producer_account >();
   const auto& incoming_executive_vote_idx = _db.get_index< executive_board_vote_index >().indices().get< by_executive_account >();
   const auto& incoming_officer_vote_idx = _db.get_index< network_officer_vote_index >().indices().get< by_officer_account >();
   const auto& incoming_subscription_idx = _db.get_index< governance_subscription_index >().indices().get< by_governance_account >();
   const auto& incoming_enterprise_vote_idx = _db.get_index< enterprise_vote_index >().indices().get< by_enterprise_id >();
   const auto& incoming_commit_violation_idx = _db.get_index< commit_violation_index >().indices().get< by_producer_height >();

   const auto& outgoing_producer_vote_idx = _db.get_index< producer_vote_index >().indices().get< by_account_rank >();
   const auto& outgoing_executive_vote_idx = _db.get_index< executive_board_vote_index >().indices().get< by_account_rank >();
   const auto& outgoing_officer_vote_idx = _db.get_index< network_officer_vote_index >().indices().get< by_account_type_rank >();
   const auto& outgoing_subscription_idx = _db.get_index< governance_subscription_index >().indices().get< by_account_rank >();
   const auto& outgoing_enterprise_vote_idx = _db.get_index< enterprise_vote_index >().indices().get< by_account_rank >();
   const auto& outgoing_commit_violation_idx = _db.get_index< commit_violation_index >().indices().get< by_reporter_height >();

   const auto& owner_history_idx = _db.get_index< account_authority_history_index >().indices().get< by_account >();
   const auto& recovery_idx = _db.get_index< account_recovery_request_index >().indices().get< by_account >();

   const auto& history_idx = _db.get_index< account_history_index >().indices().get< by_account >();

   vector< extended_account > results;

   for( auto name: names )
   {
      auto account_itr = account_idx.find( name );
      if ( account_itr != account_idx.end() )
      {
         results.push_back( extended_account( *account_itr, _db ) );

         auto balance_itr = balance_idx.lower_bound( name );
         while( balance_itr != balance_idx.end() && balance_itr->owner == name )
         {
            results.back().balances.balances[ balance_itr->symbol ] = account_balance_api_obj( *balance_itr );
            ++balance_itr;
         }
   
         auto limit_itr = limit_idx.lower_bound( name );
         while( limit_itr != limit_idx.end() && limit_itr->seller == name ) 
         {
            results.back().orders.limit_orders.push_back( limit_order_api_obj( *limit_itr ) );
            ++limit_itr;
         }

         auto margin_itr = margin_idx.lower_bound( name );
         while( margin_itr != margin_idx.end() && margin_itr->owner == name )
         {
            results.back().orders.margin_orders.push_back( margin_order_api_obj( *margin_itr ) );
            ++margin_itr;
         }

         auto call_itr = call_idx.lower_bound( name );
         while( call_itr != call_idx.end() && call_itr->borrower == name ) 
         {
            results.back().orders.call_orders.push_back( call_order_api_obj( *call_itr ) );
            ++call_itr;
         }

         auto loan_itr = loan_idx.lower_bound( name );
         while( loan_itr != loan_idx.end() && loan_itr->owner == name ) 
         {
            results.back().orders.loan_orders.push_back( credit_loan_api_obj( *loan_itr ) );
            ++loan_itr;
         }

         auto collateral_itr = collateral_idx.lower_bound( name );
         while( collateral_itr != collateral_idx.end() && collateral_itr->owner == name ) 
         {
            results.back().orders.collateral.push_back( credit_collateral_api_obj( *collateral_itr ) );
            ++collateral_itr;
         }

         auto following_itr = following_idx.find( name );
         if( following_itr != following_idx.end() )
         {
            results.back().following = account_following_api_obj( *following_itr );
         }

         auto producer_itr = producer_idx.find( name );
         if( producer_itr != producer_idx.end() )
         {
            results.back().network.producer = producer_api_obj( *producer_itr );
         }

         auto officer_itr = officer_idx.find( name );
         if( officer_itr != officer_idx.end() )
         {
            results.back().network.network_officer = network_officer_api_obj( *officer_itr );
         }

         auto executive_itr = executive_idx.find( name );
         if( executive_itr != executive_idx.end() )
         {
            results.back().network.executive_board = executive_board_api_obj( *executive_itr );
         } 

         auto interface_itr = interface_idx.find( name );
         if( interface_itr != interface_idx.end() )
         {
            results.back().network.interface = interface_api_obj( *interface_itr );
         }

         auto supernode_itr = supernode_idx.find( name );
         if( supernode_itr != supernode_idx.end() )
         {
            results.back().network.supernode = supernode_api_obj( *supernode_itr );
         }

         auto governance_itr = governance_idx.find( name );
         if( governance_itr != governance_idx.end() )
         {
            results.back().network.governance_account = governance_account_api_obj( *governance_itr );
         }

         auto enterprise_itr = enterprise_idx.lower_bound( name );
         while( enterprise_itr != enterprise_idx.end() && enterprise_itr->account == name )
         {
            results.back().network.enterprise_proposals.push_back( enterprise_api_obj( *enterprise_itr ) );
            ++enterprise_itr;
         }

         auto validation_itr = validation_idx.lower_bound( name );
         while( validation_itr != validation_idx.end() && 
            validation_itr->producer == name &&
            results.back().network.block_validations.size() < 100 )
         {
            results.back().network.block_validations.push_back( block_validation_api_obj( *validation_itr ) );
            ++validation_itr;
         }

         auto incoming_producer_vote_itr = incoming_producer_vote_idx.lower_bound( name );
         while( incoming_producer_vote_itr != incoming_producer_vote_idx.end() && 
            incoming_producer_vote_itr->producer == name )
         {
            results.back().network.incoming_producer_votes[ incoming_producer_vote_itr->account ] = producer_vote_api_obj( *incoming_producer_vote_itr );
            ++incoming_producer_vote_itr;
         }

         auto incoming_officer_vote_itr = incoming_officer_vote_idx.lower_bound( name );
         while( incoming_officer_vote_itr != incoming_officer_vote_idx.end() && 
            incoming_officer_vote_itr->network_officer == name )
         {
            results.back().network.incoming_network_officer_votes[ incoming_officer_vote_itr->account ] = network_officer_vote_api_obj( *incoming_officer_vote_itr );
            ++incoming_officer_vote_itr;
         }

         auto incoming_executive_vote_itr = incoming_executive_vote_idx.lower_bound( name );
         while( incoming_executive_vote_itr != incoming_executive_vote_idx.end() && 
            incoming_executive_vote_itr->executive_board == name )
         {
            results.back().network.incoming_executive_board_votes[ incoming_executive_vote_itr->account ] = executive_board_vote_api_obj( *incoming_executive_vote_itr );
            ++incoming_executive_vote_itr;
         }

         auto incoming_subscription_itr = incoming_subscription_idx.lower_bound( name );
         while( incoming_subscription_itr != incoming_subscription_idx.end() && 
            incoming_subscription_itr->governance_account == name )
         {
            results.back().network.incoming_governance_subscriptions[ incoming_subscription_itr->account ] = governance_subscription_api_obj( *incoming_subscription_itr );
            ++incoming_subscription_itr;
         }

         auto incoming_enterprise_vote_itr = incoming_enterprise_vote_idx.lower_bound( name );
         while( incoming_enterprise_vote_itr != incoming_enterprise_vote_idx.end() && 
            incoming_enterprise_vote_itr->account == name )
         {
            results.back().network.incoming_enterprise_votes[ incoming_enterprise_vote_itr->account ][ to_string( incoming_enterprise_vote_itr->enterprise_id ) ] = enterprise_vote_api_obj( *incoming_enterprise_vote_itr );
            ++incoming_enterprise_vote_itr;
         }

         auto incoming_commit_violation_itr = incoming_commit_violation_idx.lower_bound( name );
         while( incoming_commit_violation_itr != incoming_commit_violation_idx.end() && 
            incoming_commit_violation_itr->producer == name &&
            results.back().network.incoming_commit_violations.size() < 100 )
         {
            results.back().network.incoming_commit_violations[ incoming_commit_violation_itr->reporter ] = commit_violation_api_obj( *incoming_commit_violation_itr );
            ++incoming_commit_violation_itr;
         }

         auto outgoing_producer_vote_itr = outgoing_producer_vote_idx.lower_bound( name );
         while( outgoing_producer_vote_itr != outgoing_producer_vote_idx.end() && 
            outgoing_producer_vote_itr->account == name ) 
         {
            results.back().network.outgoing_producer_votes[ outgoing_producer_vote_itr->producer ] = producer_vote_api_obj( *outgoing_producer_vote_itr );
            ++outgoing_producer_vote_itr;
         }

         auto outgoing_officer_vote_itr = outgoing_officer_vote_idx.lower_bound( name );
         while( outgoing_officer_vote_itr != outgoing_officer_vote_idx.end() && 
            outgoing_officer_vote_itr->account == name )
         {
            results.back().network.outgoing_network_officer_votes[ outgoing_officer_vote_itr->network_officer ] = network_officer_vote_api_obj( *outgoing_officer_vote_itr );
            ++outgoing_officer_vote_itr;
         }

         auto outgoing_executive_vote_itr = outgoing_executive_vote_idx.lower_bound( name );
         while( outgoing_executive_vote_itr != outgoing_executive_vote_idx.end() && 
            outgoing_executive_vote_itr->account == name )
         {
            results.back().network.outgoing_executive_board_votes[ outgoing_executive_vote_itr->executive_board ] = executive_board_vote_api_obj( *outgoing_executive_vote_itr );
            ++outgoing_executive_vote_itr;
         }

         auto outgoing_subscription_itr = outgoing_subscription_idx.lower_bound( name );
         while( outgoing_subscription_itr != outgoing_subscription_idx.end() && 
            outgoing_subscription_itr->account == name )
         {
            results.back().network.outgoing_governance_subscriptions[ outgoing_subscription_itr->governance_account ] = governance_subscription_api_obj( *outgoing_subscription_itr );
            ++outgoing_subscription_itr;
         }

         auto outgoing_enterprise_vote_itr = outgoing_enterprise_vote_idx.lower_bound( name );
         while( outgoing_enterprise_vote_itr != outgoing_enterprise_vote_idx.end() && 
            outgoing_enterprise_vote_itr->account == name )
         {
            results.back().network.outgoing_enterprise_votes[ outgoing_enterprise_vote_itr->account ][ to_string( outgoing_enterprise_vote_itr->enterprise_id ) ] = enterprise_vote_api_obj( *outgoing_enterprise_vote_itr );
            ++outgoing_enterprise_vote_itr;
         }

         auto outgoing_commit_violation_itr = outgoing_commit_violation_idx.lower_bound( name );
         while( outgoing_commit_violation_itr != outgoing_commit_violation_idx.end() && 
            outgoing_commit_violation_itr->reporter == name &&
            results.back().network.outgoing_commit_violations.size() < 100 )
         {
            results.back().network.outgoing_commit_violations[ outgoing_commit_violation_itr->producer ] = commit_violation_api_obj( *outgoing_commit_violation_itr );
            ++outgoing_commit_violation_itr;
         }

         auto business_itr = business_idx.find( name );
         if( business_itr != business_idx.end() )
         {
            results.back().business = business_account_state( *business_itr );
         }

         auto incoming_account_exec_itr = incoming_account_exec_idx.lower_bound( name );
         while( incoming_account_exec_itr != incoming_account_exec_idx.end() && 
            incoming_account_exec_itr->executive_account == name )
         {
            results.back().business.incoming_executive_votes[ incoming_account_exec_itr->business_account ][ incoming_account_exec_itr->account ] = account_executive_vote_api_obj( *incoming_account_exec_itr );
            ++incoming_account_exec_itr;
         }

         auto incoming_account_officer_itr = incoming_account_officer_idx.lower_bound( name );
         while( incoming_account_officer_itr != incoming_account_officer_idx.end() && 
            incoming_account_officer_itr->officer_account == name )
         {
            results.back().business.incoming_officer_votes[ incoming_account_officer_itr->business_account ][ incoming_account_officer_itr->account ] = account_officer_vote_api_obj( *incoming_account_officer_itr );
            ++incoming_account_officer_itr;
         }

         auto incoming_business_exec_itr = incoming_business_exec_idx.lower_bound( name );
         while( incoming_business_exec_itr != incoming_business_exec_idx.end() && 
            incoming_business_exec_itr->business_account == name )
         {
            results.back().business.incoming_executive_votes[ incoming_business_exec_itr->executive_account ][ incoming_business_exec_itr->account ] = account_executive_vote_api_obj( *incoming_business_exec_itr );
            ++incoming_business_exec_itr;
         }

         auto incoming_business_officer_itr = incoming_business_officer_idx.lower_bound( name );
         while( incoming_business_officer_itr != incoming_business_officer_idx.end() && 
            incoming_business_officer_itr->business_account == name )
         {
            results.back().business.incoming_officer_votes[ incoming_business_officer_itr->officer_account ][ incoming_business_officer_itr->account ] = account_officer_vote_api_obj( *incoming_business_officer_itr );
            ++incoming_business_officer_itr;
         }

         auto outgoing_account_exec_itr = outgoing_account_exec_idx.lower_bound( name );
         while( outgoing_account_exec_itr != outgoing_account_exec_idx.end() && 
            outgoing_account_exec_itr->account == name )
         {
            results.back().business.outgoing_executive_votes[ outgoing_account_exec_itr->business_account ][ outgoing_account_exec_itr->executive_account ] = account_executive_vote_api_obj( *outgoing_account_exec_itr );
            ++outgoing_account_exec_itr;
         }

         auto outgoing_account_officer_itr = outgoing_account_officer_idx.lower_bound( name );
         while( outgoing_account_officer_itr != outgoing_account_officer_idx.end() && 
            outgoing_account_officer_itr->account == name )
         {
            results.back().business.outgoing_officer_votes[ outgoing_account_officer_itr->business_account ][ outgoing_account_officer_itr->officer_account ] = account_officer_vote_api_obj( *outgoing_account_officer_itr );
            ++outgoing_account_officer_itr;
         }

         auto account_req_itr = account_req_idx.lower_bound( name );
         auto bus_req_itr = bus_req_idx.lower_bound( name );

         while( account_req_itr != account_req_idx.end() && 
            account_req_itr->account == name )
         {
            results.back().business.outgoing_requests[ account_req_itr->business_account ] = account_request_api_obj( *account_req_itr );
            ++account_req_itr;
         }

         while( bus_req_itr != bus_req_idx.end() && 
            bus_req_itr->business_account == name )
         {
            results.back().business.incoming_requests[ bus_req_itr->account ] = account_request_api_obj( *bus_req_itr );
            ++bus_req_itr;
         }

         auto account_inv_itr = account_inv_idx.lower_bound( name );
         auto member_inv_itr = member_inv_idx.lower_bound( name );
         auto bus_inv_itr = bus_inv_idx.lower_bound( name );

         while( account_inv_itr != account_inv_idx.end() && 
            account_inv_itr->account == name )
         {
            results.back().business.outgoing_invites[ account_inv_itr->member ] = account_invite_api_obj( *account_inv_itr );
            ++account_inv_itr;
         }

         while( member_inv_itr != member_inv_idx.end() && 
            member_inv_itr->member == name )
         {
            results.back().business.incoming_invites[ member_inv_itr->business_account ] = account_invite_api_obj( *member_inv_itr );
            ++member_inv_itr;
         }

         while( bus_inv_itr != bus_inv_idx.end() && 
            bus_inv_itr->business_account == name )
         {
            results.back().business.outgoing_invites[ bus_inv_itr->member ] = account_invite_api_obj( *bus_inv_itr );
            ++bus_inv_itr;
         }

         auto connection_a_itr = connection_a_idx.lower_bound( boost::make_tuple( name, connection_tier_type::CONNECTION ) );
         auto connection_b_itr = connection_b_idx.lower_bound( boost::make_tuple( name, connection_tier_type::CONNECTION ) );
         while( connection_a_itr != connection_a_idx.end() && 
            connection_a_itr->account_a == name &&
            connection_a_itr->connection_type == connection_tier_type::CONNECTION )
         {
            results.back().connections.connections[ connection_a_itr->account_b ] = account_connection_api_obj( *connection_a_itr );
            results.back().keychain.connection_keys[ connection_a_itr->account_b ] = connection_a_itr->encrypted_key_b;
            ++connection_a_itr;
         }
         while( connection_b_itr != connection_b_idx.end() && 
            connection_b_itr->account_b == name &&
            connection_b_itr->connection_type == connection_tier_type::CONNECTION )
         {
            results.back().connections.connections[ connection_b_itr->account_a ] = account_connection_api_obj( *connection_b_itr );
            results.back().keychain.connection_keys[ connection_b_itr->account_a ] = connection_b_itr->encrypted_key_a;
            ++connection_b_itr;
         }

         connection_a_itr = connection_a_idx.lower_bound( boost::make_tuple( name, connection_tier_type::FRIEND ) );
         connection_b_itr = connection_b_idx.lower_bound( boost::make_tuple( name, connection_tier_type::FRIEND ) );
         while( connection_a_itr != connection_a_idx.end() && 
            connection_a_itr->account_a == name &&
            connection_a_itr->connection_type == connection_tier_type::FRIEND )
         {
            results.back().connections.friends[ connection_a_itr->account_b ] = account_connection_api_obj( *connection_a_itr );
            results.back().keychain.friend_keys[ connection_a_itr->account_b ] = connection_a_itr->encrypted_key_b;
            ++connection_a_itr;
         }
         while( connection_b_itr != connection_b_idx.end() && 
            connection_b_itr->account_b == name &&
            connection_b_itr->connection_type == connection_tier_type::FRIEND )
         {
            results.back().connections.friends[ connection_b_itr->account_a ] = account_connection_api_obj( *connection_b_itr );
            results.back().keychain.friend_keys[ connection_b_itr->account_a ] = connection_b_itr->encrypted_key_a;
            ++connection_b_itr;
         }

         connection_a_itr = connection_a_idx.lower_bound( boost::make_tuple( name, connection_tier_type::COMPANION ) );
         connection_b_itr = connection_b_idx.lower_bound( boost::make_tuple( name, connection_tier_type::COMPANION ) );
         while( connection_a_itr != connection_a_idx.end() && 
            connection_a_itr->account_a == name &&
            connection_a_itr->connection_type == connection_tier_type::COMPANION )
         {
            results.back().connections.companions[ connection_a_itr->account_b ] = account_connection_api_obj( *connection_a_itr );
            results.back().keychain.companion_keys[ connection_a_itr->account_b ] = connection_a_itr->encrypted_key_b;
            ++connection_a_itr;
         }
         while( connection_b_itr != connection_b_idx.end() && 
            connection_b_itr->account_b == name &&
            connection_b_itr->connection_type == connection_tier_type::COMPANION )
         {
            results.back().connections.companions[ connection_b_itr->account_a ] = account_connection_api_obj( *connection_b_itr );
            results.back().keychain.companion_keys[ connection_b_itr->account_a ] = connection_b_itr->encrypted_key_a;
            ++connection_b_itr;
         }

         auto connection_req_itr = connection_req_idx.lower_bound( name );
         auto connection_acc_itr = connection_acc_idx.lower_bound( name );

         while( connection_req_itr != connection_req_idx.end() && 
            connection_req_itr->requested_account == name )
         {
            results.back().connections.incoming_requests[ connection_req_itr->account ] = account_connection_request_api_obj( *connection_req_itr );
            ++connection_req_itr;
         }

         while( connection_acc_itr != connection_acc_idx.end() && 
            connection_acc_itr->account == name )
         {
            results.back().connections.outgoing_requests[ connection_acc_itr->requested_account ] = account_connection_request_api_obj( *connection_acc_itr );
            ++connection_acc_itr;
         }

         auto verifier_verified_itr = verifier_verified_idx.lower_bound( name );
         while( verifier_verified_itr != verifier_verified_idx.end() &&
            verifier_verified_itr->verifier_account == name )
         {
            results.back().connections.outgoing_verifications[ verifier_verified_itr->verified_account ] = account_verification_api_obj( *verifier_verified_itr );
            ++verifier_verified_itr;
         }

         auto verified_verifier_itr = verified_verifier_idx.lower_bound( name );
         while( verified_verifier_itr != verified_verifier_idx.end() &&
            verified_verifier_itr->verified_account == name )
         {
            results.back().connections.incoming_verifications[ verified_verifier_itr->verifier_account ] = account_verification_api_obj( *verified_verifier_itr );
            ++verified_verifier_itr;
         }

         auto community_itr = community_member_idx.begin();

         while( community_itr != community_member_idx.end() )
         {
            if( community_itr->founder == name )
            {
               results.back().communities.founded_communities.push_back( community_itr->name );
            }
            else if( community_itr->is_administrator( name ) )
            {
               results.back().communities.admin_communities.push_back( community_itr->name );
            }
            else if( community_itr->is_moderator( name ) )
            {
               results.back().communities.moderator_communities.push_back( community_itr->name );
            }
            else if( community_itr->is_member( name ) )
            {
               results.back().communities.member_communities.push_back( community_itr->name );
            }
            
            ++community_itr;
         }

         auto community_req_itr = community_req_idx.lower_bound( name );
         auto community_acc_inv_itr = community_acc_inv_idx.lower_bound( name );
         auto community_member_inv_itr = community_member_inv_idx.lower_bound( name );

         while( community_req_itr != community_req_idx.end() && 
            community_req_itr->account == name )
         {
            results.back().communities.pending_requests[ community_req_itr->account ] = community_request_api_obj( *community_req_itr );
            ++community_req_itr;
         }

         while( community_acc_inv_itr != community_acc_inv_idx.end() && 
            community_acc_inv_itr->account == name )
         {
            results.back().communities.outgoing_invites[ community_acc_inv_itr->member ] = community_invite_api_obj( *community_acc_inv_itr );
            ++community_acc_inv_itr;
         }

         while( community_member_inv_itr != community_member_inv_idx.end() && 
            community_member_inv_itr->member == name )
         {
            results.back().communities.incoming_invites[ community_member_inv_itr->community ] = community_invite_api_obj( *community_member_inv_itr );
            ++community_member_inv_itr;
         }

         auto community_key_itr = community_key_idx.lower_bound( name );
         while( community_key_itr != community_key_idx.end() && 
            community_key_itr->member == name )
         {
            results.back().keychain.community_keys[ community_key_itr->community ] = community_key_itr->encrypted_community_key;
            ++community_key_itr;
         }

         auto bus_key_itr = bus_key_idx.lower_bound( name );
         while( bus_key_itr != bus_key_idx.end() && 
            bus_key_itr->member == name )
         {
            results.back().keychain.business_keys[ bus_key_itr->business_account ] = bus_key_itr->encrypted_business_key;
            ++bus_key_itr;
         }

         auto transfer_req_itr = transfer_req_idx.lower_bound( name );
         auto transfer_from_req_itr = transfer_from_req_idx.lower_bound( name );
         auto recurring_itr = recurring_idx.lower_bound( name );
         auto recurring_to_itr = recurring_to_idx.lower_bound( name );
         auto recurring_req_itr = recurring_req_idx.lower_bound( name );
         auto recurring_from_req_itr = recurring_from_req_idx.lower_bound( name );

         while( transfer_req_itr != transfer_req_idx.end() && 
            transfer_req_itr->to == name )
         {
            results.back().transfers.outgoing_requests[ transfer_req_itr->from ] = transfer_request_api_obj( *transfer_req_itr );
            ++transfer_req_itr;
         }

         while( transfer_from_req_itr != transfer_from_req_idx.end() && 
            transfer_from_req_itr->from == name )
         {
            results.back().transfers.incoming_requests[ transfer_from_req_itr->to ] = transfer_request_api_obj( *transfer_from_req_itr );
            ++transfer_from_req_itr;
         }

         while( recurring_itr != recurring_idx.end() && 
            recurring_itr->from == name )
         {
            results.back().transfers.outgoing_recurring_transfers[ recurring_itr->to ] = transfer_recurring_api_obj( *recurring_itr );
            ++recurring_itr;
         }

         while( recurring_to_itr != recurring_to_idx.end() && 
            recurring_to_itr->to == name )
         {
            results.back().transfers.incoming_recurring_transfers[ recurring_to_itr->from ] = transfer_recurring_api_obj( *recurring_to_itr );
            ++recurring_to_itr;
         }

         while( recurring_req_itr != recurring_req_idx.end() && 
            recurring_req_itr->to == name )
         {
            results.back().transfers.outgoing_recurring_transfer_requests[ recurring_req_itr->from ] = transfer_recurring_request_api_obj( *recurring_req_itr );
            ++recurring_req_itr;
         }

         while( recurring_from_req_itr != recurring_from_req_idx.end() && 
            recurring_from_req_itr->from == name )
         {
            results.back().transfers.incoming_recurring_transfer_requests[ recurring_from_req_itr->to ] = transfer_recurring_request_api_obj( *recurring_from_req_itr );
            ++recurring_from_req_itr;
         }

         auto inbox_itr = inbox_idx.lower_bound( name );
         auto outbox_itr = outbox_idx.lower_bound( name );
         vector< message_api_obj > inbox;
         vector< message_api_obj > outbox;
         map< account_name_type, vector< message_api_obj > > conversations;

         while( inbox_itr != inbox_idx.end() && inbox_itr->recipient == name )
         {
            inbox.push_back( message_api_obj( *inbox_itr ) );
         }

         while( outbox_itr != outbox_idx.end() && outbox_itr->sender == name )
         {
            outbox.push_back( message_api_obj( *outbox_itr ) );
         }

         for( auto message : inbox )
         {
            conversations[ message.sender ].push_back( message );
         }

         for( auto message : outbox )
         {
            conversations[ message.recipient ].push_back( message );
         }

         for( auto conv : conversations )
         {
            vector< message_api_obj > thread = conv.second;
            std::sort( thread.begin(), thread.end(), [&]( message_api_obj a, message_api_obj b )
            {
               return a.created < b.created;
            });
            conversations[ conv.first ] = thread;
         }

         message_state mstate;
         mstate.inbox = inbox;
         mstate.outbox = outbox;
         mstate.conversations = conversations;
         results.back().messages = mstate;

         auto moderator_itr = moderator_idx.lower_bound( name );
         while( moderator_itr != moderator_idx.end() && moderator_itr->account == name )
         {
            results.back().communities.outgoing_moderator_votes[ moderator_itr->community ][ moderator_itr->moderator ] = moderator_itr->vote_rank;
            ++moderator_itr;
         }

         auto history_itr = history_idx.lower_bound( name );
   
         map< uint32_t, applied_operation> operation_history;
         
         while( history_itr != history_idx.end() && history_itr->account == name )
         {
            operation_history[ history_itr->sequence ] = _db.get( history_itr->op );
            ++history_itr;
         }

         auto owner_history_itr = owner_history_idx.lower_bound( name );

         while( owner_history_itr != owner_history_idx.end() && 
            owner_history_itr->account == name )
         {
            results.back().owner_history.push_back( owner_authority_history_api_obj( *owner_history_itr ) );
            ++owner_history_itr;
         }

         auto recovery_itr = recovery_idx.find( name );

         if( recovery_itr != recovery_idx.end() )
         {
            results.back().recovery = account_recovery_request_api_obj( *recovery_itr );
         }

         for( auto& item : operation_history )
         {
            switch( item.second.op.which() )
            {
               case operation::tag<account_create_operation>::value:
               case operation::tag<account_update_operation>::value:
               case operation::tag<account_membership_operation>::value:
               case operation::tag<account_vote_executive_operation>::value:
               case operation::tag<account_vote_officer_operation>::value:
               case operation::tag<account_member_request_operation>::value:
               case operation::tag<account_member_invite_operation>::value:
               case operation::tag<account_accept_request_operation>::value:
               case operation::tag<account_accept_invite_operation>::value:
               case operation::tag<account_remove_member_operation>::value:
               case operation::tag<account_update_list_operation>::value:
               case operation::tag<account_request_recovery_operation>::value:
               case operation::tag<account_recover_operation>::value:
               case operation::tag<account_reset_operation>::value:
               case operation::tag<account_reset_update_operation>::value:
               case operation::tag<account_recovery_update_operation>::value:
               case operation::tag<account_decline_voting_operation>::value:
               {
                  results.back().operations.account_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<account_connection_request_operation>::value:
               case operation::tag<account_connection_accept_operation>::value:
               {
                  results.back().operations.connection_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<account_follow_operation>::value:
               case operation::tag<account_follow_tag_operation>::value:
               {
                  results.back().operations.follow_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<account_activity_operation>::value:
               {
                  results.back().operations.activity_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<comment_operation>::value:
               case operation::tag<author_reward_operation>::value:
               case operation::tag<content_reward_operation>::value:
               case operation::tag<comment_reward_operation>::value:
               case operation::tag<comment_benefactor_reward_operation>::value:
               case operation::tag<list_operation>::value:
               case operation::tag<poll_operation>::value:
               case operation::tag<poll_vote_operation>::value:
               case operation::tag<premium_purchase_operation>::value:
               case operation::tag<premium_release_operation>::value:
               {
                  results.back().operations.post_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<comment_vote_operation>::value:
               case operation::tag<vote_reward_operation>::value:
               {
                  results.back().operations.vote_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<comment_view_operation>::value:
               case operation::tag<view_reward_operation>::value:
               {
                  results.back().operations.view_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<comment_share_operation>::value:
               case operation::tag<share_reward_operation>::value:
               {
                  results.back().operations.share_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<comment_moderation_operation>::value:
               case operation::tag<moderation_reward_operation>::value:
               {
                  results.back().operations.moderation_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<message_operation>::value:
               {
                  results.back().operations.message_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<community_create_operation>::value:
               case operation::tag<community_update_operation>::value:
               case operation::tag<community_add_mod_operation>::value:
               case operation::tag<community_add_admin_operation>::value:
               case operation::tag<community_vote_mod_operation>::value:
               case operation::tag<community_transfer_ownership_operation>::value:
               case operation::tag<community_join_request_operation>::value:
               case operation::tag<community_join_accept_operation>::value:
               case operation::tag<community_join_invite_operation>::value:
               case operation::tag<community_invite_accept_operation>::value:
               case operation::tag<community_remove_member_operation>::value:
               case operation::tag<community_blacklist_operation>::value:
               case operation::tag<community_subscribe_operation>::value:
               case operation::tag<community_event_operation>::value:
               case operation::tag<community_event_attend_operation>::value:
               {
                  results.back().operations.community_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<ad_creative_operation>::value:
               case operation::tag<ad_campaign_operation>::value:
               case operation::tag<ad_inventory_operation>::value:
               case operation::tag<ad_audience_operation>::value:
               case operation::tag<ad_bid_operation>::value:
               {
                  results.back().operations.ad_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<graph_node_operation>::value:
               case operation::tag<graph_edge_operation>::value:
               case operation::tag<graph_node_property_operation>::value:
               case operation::tag<graph_edge_property_operation>::value:
               {
                  results.back().operations.graph_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<transfer_operation>::value:
               case operation::tag<transfer_request_operation>::value:
               case operation::tag<transfer_accept_operation>::value:
               case operation::tag<transfer_recurring_operation>::value:
               case operation::tag<transfer_recurring_request_operation>::value:
               case operation::tag<transfer_recurring_accept_operation>::value:
               case operation::tag<transfer_confidential_operation>::value:
               case operation::tag<transfer_to_confidential_operation>::value:
               case operation::tag<transfer_from_confidential_operation>::value:
               {
                  results.back().operations.transfer_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<claim_reward_balance_operation>::value:
               case operation::tag<stake_asset_operation>::value:
               case operation::tag<unstake_asset_operation>::value:
               case operation::tag<transfer_to_savings_operation>::value:
               case operation::tag<transfer_from_savings_operation>::value:
               case operation::tag<delegate_asset_operation>::value:
               {
                  results.back().operations.balance_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<product_sale_operation>::value:
               case operation::tag<product_purchase_operation>::value:
               case operation::tag<product_auction_sale_operation>::value:
               case operation::tag<product_auction_bid_operation>::value:
               {
                  results.back().operations.product_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<escrow_transfer_operation>::value:
               case operation::tag<escrow_approve_operation>::value:
               case operation::tag<escrow_dispute_operation>::value:
               case operation::tag<escrow_release_operation>::value:
               {
                  results.back().operations.escrow_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<limit_order_operation>::value:
               case operation::tag<margin_order_operation>::value:
               case operation::tag<auction_order_operation>::value:
               case operation::tag<call_order_operation>::value:
               case operation::tag<fill_order_operation>::value:
               {
                  results.back().operations.trading_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<liquidity_pool_create_operation>::value:
               case operation::tag<liquidity_pool_exchange_operation>::value:
               case operation::tag<liquidity_pool_fund_operation>::value:
               case operation::tag<liquidity_pool_withdraw_operation>::value:
               {
                  results.back().operations.liquidity_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<credit_pool_collateral_operation>::value:
               case operation::tag<credit_pool_borrow_operation>::value:
               case operation::tag<credit_pool_lend_operation>::value:
               case operation::tag<credit_pool_withdraw_operation>::value:
               {
                  results.back().operations.credit_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<option_order_operation>::value:
               case operation::tag<option_pool_create_operation>::value:
               case operation::tag<asset_option_exercise_operation>::value:
               {
                  results.back().operations.option_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<prediction_pool_create_operation>::value:
               case operation::tag<prediction_pool_exchange_operation>::value:
               case operation::tag<prediction_pool_resolve_operation>::value:
               {
                  results.back().operations.prediction_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<asset_create_operation>::value:
               case operation::tag<asset_update_operation>::value:
               case operation::tag<asset_issue_operation>::value:
               case operation::tag<asset_reserve_operation>::value:
               case operation::tag<asset_update_issuer_operation>::value:
               case operation::tag<asset_distribution_operation>::value:
               case operation::tag<asset_distribution_fund_operation>::value:
               case operation::tag<asset_stimulus_fund_operation>::value:
               case operation::tag<asset_update_feed_producers_operation>::value:
               case operation::tag<asset_publish_feed_operation>::value:
               case operation::tag<asset_settle_operation>::value:
               case operation::tag<asset_global_settle_operation>::value:
               case operation::tag<asset_collateral_bid_operation>::value:
               {
                  results.back().operations.asset_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<account_producer_vote_operation>::value:
               case operation::tag<account_update_proxy_operation>::value:
               case operation::tag<network_officer_update_operation>::value:
               case operation::tag<network_officer_vote_operation>::value:
               case operation::tag<executive_board_update_operation>::value:
               case operation::tag<executive_board_vote_operation>::value:
               case operation::tag<governance_update_operation>::value:
               case operation::tag<governance_subscribe_operation>::value:
               case operation::tag<supernode_update_operation>::value:
               case operation::tag<supernode_reward_operation>::value:
               case operation::tag<interface_update_operation>::value:
               case operation::tag<mediator_update_operation>::value:
               case operation::tag<enterprise_update_operation>::value:
               case operation::tag<enterprise_vote_operation>::value:
               case operation::tag<enterprise_fund_operation>::value:
               case operation::tag<producer_update_operation>::value:
               case operation::tag<proof_of_work_operation>::value:
               case operation::tag<producer_reward_operation>::value:
               case operation::tag<verify_block_operation>::value:
               case operation::tag<commit_block_operation>::value:
               case operation::tag<producer_violation_operation>::value:
               {
                  results.back().operations.network_history[ item.first ] = item.second;
               }
               break;
               case operation::tag<custom_operation>::value:
               case operation::tag<custom_json_operation>::value:
               default:
                  results.back().operations.other_history[ item.first ] = item.second;
            }
         }
      }
   }
   return results;
}

map< uint32_t, applied_operation > database_api::get_account_history( string account, uint64_t from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_account_history( account, from, limit );
   });
}

map< uint32_t, applied_operation > database_api_impl::get_account_history( string account, uint64_t from, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 10000 ) );
   FC_ASSERT( from >= limit,
      "From must be greater than limit." );

   const auto& history_idx = _db.get_index< account_history_index >().indices().get< by_account >();
   auto history_itr = history_idx.lower_bound( boost::make_tuple( account, from ) );

   uint32_t n = 0;
   map< uint32_t, applied_operation > results;
   
   while( true )
   {
      if( history_itr == history_idx.end() )
      {
         break;
      }
      if( history_itr->account != account )
      {
         break;
      }
      if( n >= limit )
      {
         break;
      }
      results[ history_itr->sequence ] = _db.get( history_itr->op );
      ++history_itr;
      ++n;
   }
   return results;
}

vector< balance_state > database_api::get_balances( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_balances( names );
   });
}

vector< balance_state > database_api_impl::get_balances( vector< string > names )const
{
   const auto& balance_idx = _db.get_index< account_balance_index >().indices().get< by_owner >();
   const auto& withdraw_route_idx = _db.get_index< unstake_asset_route_index >().indices().get< by_withdraw_route >();
   const auto& destination_route_idx = _db.get_index< unstake_asset_route_index >().indices().get< by_destination >();
   const auto& savings_withdrawals_from_idx = _db.get_index< savings_withdraw_index >().indices().get< by_request_id >();
   const auto& savings_withdrawals_to_idx = _db.get_index< savings_withdraw_index >().indices().get< by_to_complete >();
   const auto& delegation_from_idx = _db.get_index< asset_delegation_index >().indices().get< by_delegator >();
   const auto& delegation_to_idx = _db.get_index< asset_delegation_index >().indices().get< by_delegatee >();
   const auto& expiration_from_idx = _db.get_index< asset_delegation_expiration_index >().indices().get< by_delegator >();
   const auto& expiration_to_idx = _db.get_index< asset_delegation_expiration_index >().indices().get< by_delegatee >();
   
   vector< balance_state > results;

   for( auto name: names )
   {
      balance_state bstate;

      auto balance_itr = balance_idx.lower_bound( name );
      while( balance_itr != balance_idx.end() && balance_itr->owner == name )
      {
         bstate.balances[ balance_itr->symbol ] = account_balance_api_obj( *balance_itr );
      }

      const account_object& acc = _db.get_account( name );

      auto withdraw_route_itr = withdraw_route_idx.lower_bound( acc.name );
      while( withdraw_route_itr != withdraw_route_idx.end() && 
         withdraw_route_itr->from == acc.name )
      {
         withdraw_route r;
         r.from = withdraw_route_itr->from;
         r.to = withdraw_route_itr->to;
         r.percent = withdraw_route_itr->percent;
         r.auto_stake = withdraw_route_itr->auto_stake;
         bstate.withdraw_routes.push_back( r );
         ++withdraw_route_itr;
      }

      auto destination_route_itr = destination_route_idx.lower_bound( acc.name );
      while( destination_route_itr != destination_route_idx.end() && 
         destination_route_itr->to == acc.name )
      {
         withdraw_route r;
         r.from = destination_route_itr->from;
         r.to = destination_route_itr->to;
         r.percent = destination_route_itr->percent;
         r.auto_stake = destination_route_itr->auto_stake;
         bstate.withdraw_routes.push_back( r );
         ++destination_route_itr;
      }

      auto savings_withdrawals_from_itr = savings_withdrawals_from_idx.lower_bound( name );
      while( savings_withdrawals_from_itr != savings_withdrawals_from_idx.end() && 
         savings_withdrawals_from_itr->from == name )
      {
         bstate.savings_withdrawals_from.push_back( savings_withdraw_api_obj( *savings_withdrawals_from_itr ) );
         ++savings_withdrawals_from_itr;
      }

      auto savings_withdrawals_to_itr = savings_withdrawals_to_idx.lower_bound( name );
      while( savings_withdrawals_to_itr != savings_withdrawals_to_idx.end() && 
         savings_withdrawals_to_itr->to == name ) 
      {
         bstate.savings_withdrawals_to.push_back( savings_withdraw_api_obj( *savings_withdrawals_to_itr ) );
         ++savings_withdrawals_to_itr;
      }

      auto delegation_from_itr = delegation_from_idx.lower_bound( name );
      while( delegation_from_itr != delegation_from_idx.end() && 
         delegation_from_itr->delegator == name )
      {
         bstate.delegations_from.push_back( *delegation_from_itr );
         ++delegation_from_itr;
      }

      auto delegation_to_itr = delegation_to_idx.lower_bound( name );
      while( delegation_to_itr != delegation_to_idx.end() && 
         delegation_to_itr->delegatee == name )
      {
         bstate.delegations_to.push_back( *delegation_to_itr );
         ++delegation_to_itr;
      }

      auto expiration_from_itr = expiration_from_idx.lower_bound( name );
      while( expiration_from_itr != expiration_from_idx.end() && 
         expiration_from_itr->delegator == name )
      {
         bstate.expirations_from.push_back( *expiration_from_itr );
         ++expiration_from_itr;
      }

      auto expiration_to_itr = expiration_to_idx.lower_bound( name );
      while( expiration_to_itr != expiration_to_idx.end() && 
         expiration_to_itr->delegator == name )
      {
         bstate.expirations_to.push_back( *expiration_to_itr );
         ++expiration_to_itr;
      }

      results.push_back( bstate );
   }

   return results;
}


vector< confidential_balance_api_obj > database_api::get_confidential_balances( const confidential_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_confidential_balances( query );
   });
}

vector< confidential_balance_api_obj > database_api_impl::get_confidential_balances( const confidential_query& query )const
{
   const auto& commit_idx = _db.get_index< confidential_balance_index >().indices().get< by_commitment >();
   const auto& key_idx = _db.get_index< confidential_balance_index >().indices().get< by_key_auth >();
   const auto& account_idx = _db.get_index< confidential_balance_index >().indices().get< by_account_auth >();

   vector< confidential_balance_api_obj > results;

   for( auto commit: query.select_commitments )
   {
      auto commit_itr = commit_idx.lower_bound( commit );
      while( commit_itr!= commit_idx.end() &&
         commit_itr->commitment == commit )
      {
         results.push_back( confidential_balance_api_obj( *commit_itr ) );
         ++commit_itr;
      }
   }

   for( auto key: query.select_key_auths )
   {
      auto key_itr = key_idx.lower_bound( public_key_type( key ) );
      while( key_itr!= key_idx.end() &&
         key_itr->key_auth() == public_key_type( key ) )
      {
         results.push_back( confidential_balance_api_obj( *key_itr ) );
         ++key_itr;
      }
   }

   for( auto acc: query.select_account_auths )
   {
      auto account_itr = account_idx.lower_bound( acc );
      while( account_itr!= account_idx.end() &&
         account_itr->account_auth() == acc )
      {
         results.push_back( confidential_balance_api_obj( *account_itr ) );
         ++account_itr;
      }
   }

   return results;
}

vector< message_state > database_api::get_messages( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_messages( names );
   });
}

vector< message_state > database_api_impl::get_messages( vector< string > names )const
{
   const auto& inbox_idx = _db.get_index< message_index >().indices().get< by_account_inbox >();
   const auto& outbox_idx = _db.get_index< message_index >().indices().get< by_account_outbox >();

   vector< message_state > results;

   for( auto name: names )
   {
      auto inbox_itr = inbox_idx.lower_bound( name );
      auto outbox_itr = outbox_idx.lower_bound( name );
      vector< message_api_obj > inbox;
      vector< message_api_obj > outbox;
      map< account_name_type, vector< message_api_obj > > conversations;

      while( inbox_itr != inbox_idx.end() && inbox_itr->recipient == name )
      {
         inbox.push_back( message_api_obj( *inbox_itr ) );
      }

      while( outbox_itr != outbox_idx.end() && outbox_itr->sender == name )
      {
         outbox.push_back( message_api_obj( *outbox_itr ) );
      }

      for( auto message : inbox )
      {
         conversations[ message.sender ].push_back( message );
      }

      for( auto message : outbox )
      {
         conversations[ message.recipient ].push_back( message );
      }

      for( auto conv : conversations )
      {
         vector< message_api_obj > thread = conv.second;
         std::sort( thread.begin(), thread.end(), [&]( message_api_obj a, message_api_obj b )
         {
            return a.created < b.created;
         });
         conversations[ conv.first ] = thread;
      }

      message_state mstate;
      mstate.inbox = inbox;
      mstate.outbox = outbox;
      mstate.conversations = conversations;
      results.push_back( mstate );
   }

   return results;
}


list_state database_api::get_list( string name, string list_id )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_list( name, list_id );
   });
}


list_state database_api_impl::get_list( string name, string list_id )const
{
   const auto& list_idx = _db.get_index< list_index >().indices().get< by_list_id >();

   list_state lstate;
   
   auto list_itr = list_idx.find( boost::make_tuple( name, list_id ) );

   if( list_itr != list_idx.end() )
   {
      const list_object& list = *list_itr;

      lstate.creator = list.creator;
      lstate.list_id = to_string( list.list_id );
      lstate.name = to_string( list.name );

      for( account_id_type id : list.accounts )
      {
         lstate.accounts.push_back( account_api_obj( _db.get( id ), _db ) );
      }
      for( comment_id_type id : list.comments )
      {
         lstate.comments.push_back( comment_api_obj( _db.get( id ) ) );
      }
      for( community_id_type id : list.communities )
      {
         lstate.communities.push_back( community_api_obj( _db.get( id ) ) );
      }
      for( asset_id_type id : list.assets )
      {
         lstate.assets.push_back( asset_api_obj( _db.get( id ) ) );
      }
      for( product_sale_id_type id : list.products )
      {
         lstate.products.push_back( product_sale_api_obj( _db.get( id ) ) );
      }
      for( product_auction_sale_id_type id : list.auctions )
      {
         lstate.auctions.push_back( product_auction_sale_api_obj( _db.get( id ) ) );
      }
      for( graph_node_id_type id : list.nodes )
      {
         lstate.nodes.push_back( graph_node_api_obj( _db.get( id ) ) );
      }
      for( graph_edge_id_type id : list.edges )
      {
         lstate.edges.push_back( graph_edge_api_obj( _db.get( id ) ) );
      }
      for( graph_node_property_id_type id : list.node_types )
      {
         lstate.node_types.push_back( graph_node_property_api_obj( _db.get( id ) ) );
      }
      for( graph_edge_property_id_type id : list.edge_types )
      {
         lstate.edge_types.push_back( graph_edge_property_api_obj( _db.get( id ) ) );
      }
   }

   return lstate;
}


vector< account_list_state > database_api::get_account_lists( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_account_lists( names );
   });
}


vector< account_list_state > database_api_impl::get_account_lists( vector< string > names )const
{
   const auto& list_idx = _db.get_index< list_index >().indices().get< by_list_id >();
   auto list_itr = list_idx.begin();
   
   vector< account_list_state > results;

   for( auto name : names )
   {
      list_itr = list_idx.lower_bound( name );

      account_list_state account_lstate;

      while( list_itr != list_idx.end() &&
         list_itr->creator == name )
      {
         const list_object& list = *list_itr;

         list_state lstate;

         lstate.creator = list.creator;
         lstate.list_id = to_string( list.list_id );
         lstate.name = to_string( list.name );

         for( account_id_type id : list.accounts )
         {
            lstate.accounts.push_back( account_api_obj( _db.get( id ), _db ) );
         }
         for( comment_id_type id : list.comments )
         {
            lstate.comments.push_back( comment_api_obj( _db.get( id ) ) );
         }
         for( community_id_type id : list.communities )
         {
            lstate.communities.push_back( community_api_obj( _db.get( id ) ) );
         }
         for( asset_id_type id : list.assets )
         {
            lstate.assets.push_back( asset_api_obj( _db.get( id ) ) );
         }
         for( product_sale_id_type id : list.products )
         {
            lstate.products.push_back( product_sale_api_obj( _db.get( id ) ) );
         }
         for( product_auction_sale_id_type id : list.auctions )
         {
            lstate.auctions.push_back( product_auction_sale_api_obj( _db.get( id ) ) );
         }
         for( graph_node_id_type id : list.nodes )
         {
            lstate.nodes.push_back( graph_node_api_obj( _db.get( id ) ) );
         }
         for( graph_edge_id_type id : list.edges )
         {
            lstate.edges.push_back( graph_edge_api_obj( _db.get( id ) ) );
         }
         for( graph_node_property_id_type id : list.node_types )
         {
            lstate.node_types.push_back( graph_node_property_api_obj( _db.get( id ) ) );
         }
         for( graph_edge_property_id_type id : list.edge_types )
         {
            lstate.edge_types.push_back( graph_edge_property_api_obj( _db.get( id ) ) );
         }

         account_lstate.lists.push_back( lstate );

         ++list_itr;
      }

      results.push_back( account_lstate );
   }

   return results;
}


poll_state database_api::get_poll( string name, string poll_id )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_poll( name, poll_id );
   });
}


poll_state database_api_impl::get_poll( string name, string poll_id )const
{
   const auto& poll_idx = _db.get_index< poll_index >().indices().get< by_poll_id >();
   const auto& vote_idx = _db.get_index< poll_vote_index >().indices().get< by_poll_id >();
   
   poll_state pstate;
   
   auto poll_itr = poll_idx.find( boost::make_tuple( name, poll_id ) );
   auto vote_itr = vote_idx.begin();

   if( poll_itr != poll_idx.end() )
   {
      const poll_object& poll = *poll_itr;
      pstate = poll_state( poll );

      vote_itr = vote_idx.lower_bound( boost::make_tuple( name, poll_id ) );

      while( vote_itr != vote_idx.end() &&
         vote_itr->creator == name &&
         to_string( vote_itr->poll_id ) == poll_id )
      {
         pstate.vote_count[ vote_itr->poll_option ]++;
         pstate.votes.push_back( *vote_itr );
         ++vote_itr;
      }
   }

   return pstate;
}


vector< account_poll_state > database_api::get_account_polls( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_account_polls( names );
   });
}


vector< account_poll_state > database_api_impl::get_account_polls( vector< string > names )const
{
   const auto& poll_idx = _db.get_index< poll_index >().indices().get< by_poll_id >();
   const auto& vote_idx = _db.get_index< poll_vote_index >().indices().get< by_poll_id >();
   
   vector< account_poll_state > results;

   for( auto name : names )
   {
      auto poll_itr = poll_idx.lower_bound( name );

      account_poll_state account_pstate;

      while( poll_itr != poll_idx.end() &&
         poll_itr->creator == name )
      {
         const poll_object& poll = *poll_itr;
         poll_state pstate = poll_state( poll );

         auto vote_itr = vote_idx.lower_bound( boost::make_tuple( name, poll.poll_id ) );

         while( vote_itr != vote_idx.end() &&
            vote_itr->creator == name &&
            vote_itr->poll_id == poll.poll_id )
         {
            pstate.vote_count[ vote_itr->poll_option ] += 1;
            pstate.votes.push_back( *vote_itr );
            ++vote_itr;
         }

         account_pstate.polls.push_back( pstate );

         ++poll_itr;
      }

      results.push_back( account_pstate );
   }

   return results;
}


vector< key_state > database_api::get_keychains( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_keychains( names );
   });
}


vector< key_state > database_api_impl::get_keychains( vector< string > names )const
{
   const auto& connection_a_idx = _db.get_index< account_connection_index >().indices().get< by_account_a >();
   const auto& connection_b_idx = _db.get_index< account_connection_index >().indices().get< by_account_b >();
   const auto& community_idx = _db.get_index< community_member_key_index >().indices().get< by_member_community >();
   const auto& business_idx = _db.get_index< account_member_key_index >().indices().get< by_member_business >();

   vector< key_state > results;

   for( auto name : names )
   {
      key_state kstate;

      auto connection_a_itr = connection_a_idx.lower_bound( boost::make_tuple( name, connection_tier_type::CONNECTION ) );
      auto connection_b_itr = connection_b_idx.lower_bound( boost::make_tuple( name, connection_tier_type::CONNECTION ) );
      while( connection_a_itr != connection_a_idx.end() && 
         connection_a_itr->account_a == name &&
         connection_a_itr->connection_type == connection_tier_type::CONNECTION )
      {
         kstate.connection_keys[ connection_a_itr->account_b ] = connection_a_itr->encrypted_key_b;
         ++connection_a_itr;
      }
      while( connection_b_itr != connection_b_idx.end() && 
         connection_b_itr->account_b == name &&
         connection_b_itr->connection_type == connection_tier_type::CONNECTION )
      {
         kstate.connection_keys[ connection_b_itr->account_a ] = connection_b_itr->encrypted_key_a;
         ++connection_b_itr;
      }

      connection_a_itr = connection_a_idx.lower_bound( boost::make_tuple( name, connection_tier_type::FRIEND ) );
      connection_b_itr = connection_b_idx.lower_bound( boost::make_tuple( name, connection_tier_type::FRIEND ) );
      while( connection_a_itr != connection_a_idx.end() && 
         connection_a_itr->account_a == name &&
         connection_a_itr->connection_type == connection_tier_type::FRIEND )
      {
         kstate.friend_keys[ connection_a_itr->account_b ] = connection_a_itr->encrypted_key_b;
         ++connection_a_itr;
      }
      while( connection_b_itr != connection_b_idx.end() && 
         connection_b_itr->account_b == name &&
         connection_b_itr->connection_type == connection_tier_type::FRIEND )
      {
         kstate.friend_keys[ connection_b_itr->account_a ] = connection_b_itr->encrypted_key_a;
         ++connection_b_itr;
      }

      connection_a_itr = connection_a_idx.lower_bound( boost::make_tuple( name, connection_tier_type::COMPANION ) );
      connection_b_itr = connection_b_idx.lower_bound( boost::make_tuple( name, connection_tier_type::COMPANION ) );
      while( connection_a_itr != connection_a_idx.end() && 
         connection_a_itr->account_a == name &&
         connection_a_itr->connection_type == connection_tier_type::COMPANION )
      {
         kstate.companion_keys[ connection_a_itr->account_b ] = connection_a_itr->encrypted_key_b;
         ++connection_a_itr;
      }
      while( connection_b_itr != connection_b_idx.end() && 
         connection_b_itr->account_b == name &&
         connection_b_itr->connection_type == connection_tier_type::COMPANION )
      {
         kstate.companion_keys[ connection_b_itr->account_a ] = connection_b_itr->encrypted_key_a;
         ++connection_b_itr;
      }

      auto community_itr = community_idx.lower_bound( name );
      while( community_itr != community_idx.end() && 
         community_itr->member == name )
      {
         kstate.community_keys[ community_itr->community ] = community_itr->encrypted_community_key;
         ++community_itr;
      }

      auto business_itr = business_idx.lower_bound( name );
      while( business_itr != business_idx.end() && 
         business_itr->member == name )
      {
         kstate.business_keys[ business_itr->business_account ] = business_itr->encrypted_business_key;
         ++business_itr;
      }
      results.push_back( kstate );
   }
   return results;
}

set< string > database_api::lookup_accounts( string lower_bound_name, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->lookup_accounts( lower_bound_name, limit );
   });
}

set< string > database_api_impl::lookup_accounts( string lower_bound_name, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 1000 ) );
   const auto& accounts_by_name = _db.get_index< account_index >().indices().get< by_name >();
   set< string > results;

   for( auto acc_itr = accounts_by_name.lower_bound( lower_bound_name );
      limit-- && acc_itr != accounts_by_name.end();
      ++acc_itr )
   {
      results.insert( acc_itr->name );
   }

   return results;
}

uint64_t database_api::get_account_count()const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_account_count();
   });
}

uint64_t database_api_impl::get_account_count()const
{
   return _db.get_index< account_index >().indices().size();
}


   //================//
   // === Assets === //
   //================//


vector< extended_asset > database_api::get_assets( vector< string > assets )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_assets( assets );
   });
}

vector< extended_asset > database_api_impl::get_assets( vector< string > assets )const
{ 
   vector< extended_asset > results;

   const auto& asset_idx = _db.get_index< asset_index >().indices().get< by_symbol >();
   const auto& asset_dyn_idx = _db.get_index< asset_dynamic_data_index >().indices().get< by_symbol >();

   for( auto asset : assets )
   {
      auto asset_itr = asset_idx.find( asset );
      if( asset_itr != asset_idx.end() )
      {
         results.push_back( extended_asset( *asset_itr ) );
      }
      
      auto asset_dyn_itr = asset_dyn_idx.find( asset );
      if( asset_dyn_itr != asset_dyn_idx.end() )
      {
         results.back().total_supply = asset_dyn_itr->get_total_supply().amount.value;
         results.back().liquid_supply = asset_dyn_itr->liquid_supply.value;
         results.back().reward_supply = asset_dyn_itr->reward_supply.value;
         results.back().savings_supply = asset_dyn_itr->savings_supply.value;
         results.back().delegated_supply = asset_dyn_itr->delegated_supply.value;
         results.back().receiving_supply = asset_dyn_itr->receiving_supply.value;
         results.back().pending_supply = asset_dyn_itr->pending_supply.value;
         results.back().confidential_supply = asset_dyn_itr->confidential_supply.value;
      }

      const auto& currency_idx = _db.get_index< asset_currency_data_index >().indices().get< by_symbol >();
      auto currency_itr = currency_idx.find( asset );
      if( currency_itr != currency_idx.end() )
      {
         results.back().currency = currency_data_api_obj( *currency_itr );
      }

      const auto& stablecoin_idx = _db.get_index< asset_stablecoin_data_index >().indices().get< by_symbol >();
      auto stablecoin_itr = stablecoin_idx.find( asset );
      if( stablecoin_itr != stablecoin_idx.end() )
      {
         results.back().stablecoin = stablecoin_data_api_obj( *stablecoin_itr );
      }

      const auto& equity_idx = _db.get_index< asset_equity_data_index >().indices().get< by_symbol >();
      auto equity_itr = equity_idx.find( asset );
      if( equity_itr != equity_idx.end() )
      {
         results.back().equity = equity_data_api_obj( *equity_itr );
      }

      const auto& bond_idx = _db.get_index< asset_bond_data_index >().indices().get< by_symbol >();
      auto bond_itr = bond_idx.find( asset );
      if( bond_itr != bond_idx.end() )
      {
         results.back().bond = bond_data_api_obj( *bond_itr );
      }

      const auto& credit_idx = _db.get_index< asset_credit_data_index >().indices().get< by_symbol >();
      auto credit_itr = credit_idx.find( asset );
      if( credit_itr != credit_idx.end() )
      {
         results.back().credit = credit_data_api_obj( *credit_itr );
      }

      const auto& stimulus_idx = _db.get_index< asset_stimulus_data_index >().indices().get< by_symbol >();
      auto stimulus_itr = stimulus_idx.find( asset );
      if( stimulus_itr != stimulus_idx.end() )
      {
         results.back().stimulus = stimulus_data_api_obj( *stimulus_itr );
      }

      const auto& unique_idx = _db.get_index< asset_unique_data_index >().indices().get< by_symbol >();
      auto unique_itr = unique_idx.find( asset );
      if( unique_itr != unique_idx.end() )
      {
         results.back().unique = unique_data_api_obj( *unique_itr );
      }

      const auto& credit_pool_idx = _db.get_index< asset_credit_pool_index >().indices().get< by_base_symbol >();
      auto credit_pool_itr = credit_pool_idx.find( asset );
      if( credit_pool_itr != credit_pool_idx.end() )
      {
         results.back().credit_pool = credit_pool_api_obj( *credit_pool_itr );
      }

      const auto& pool_a_idx = _db.get_index< asset_liquidity_pool_index >().indices().get< by_symbol_a >();
      const auto& pool_b_idx = _db.get_index< asset_liquidity_pool_index >().indices().get< by_symbol_b >();
      auto pool_a_itr = pool_a_idx.lower_bound( asset );
      auto pool_b_itr = pool_b_idx.lower_bound( asset );

      while( pool_a_itr != pool_a_idx.end() && pool_a_itr->symbol_a == asset )
      {
         results.back().liquidity_pools[ pool_a_itr->symbol_b ] = liquidity_pool_api_obj( *pool_a_itr );
      }

      while( pool_b_itr != pool_b_idx.end() && pool_b_itr->symbol_b == asset )
      {
         results.back().liquidity_pools[ pool_b_itr->symbol_a ] = liquidity_pool_api_obj( *pool_b_itr );
      }

      const auto& base_idx = _db.get_index< asset_option_pool_index >().indices().get< by_base_symbol >();
      const auto& quote_idx = _db.get_index< asset_option_pool_index >().indices().get< by_quote_symbol >();
      auto base_itr = base_idx.lower_bound( asset );
      auto quote_itr = quote_idx.lower_bound( asset );

      while( base_itr != base_idx.end() && base_itr->base_symbol == asset )
      {
         results.back().option_pools[ base_itr->quote_symbol ] = option_pool_api_obj( *base_itr );
      }

      while( quote_itr != quote_idx.end() && quote_itr->quote_symbol == asset )
      {
         results.back().option_pools[ quote_itr->base_symbol ] = option_pool_api_obj( *quote_itr );
      }

      const auto& prediction_idx = _db.get_index< asset_prediction_pool_index >().indices().get< by_prediction_symbol >();
      auto prediction_itr = prediction_idx.find( asset );
      if( prediction_itr != prediction_idx.end() )
      {
         results.back().prediction = prediction_pool_api_obj( *prediction_itr );
      }

      const auto& resolution_idx = _db.get_index< asset_prediction_pool_resolution_index >().indices().get< by_prediction_symbol >();
      auto resolution_itr = resolution_idx.lower_bound( asset );

      while( resolution_itr != resolution_idx.end() && resolution_itr->prediction_symbol == asset )
      {
         results.back().resolutions[ resolution_itr->resolution_outcome ] = prediction_pool_resolution_api_obj( *resolution_itr );
      }

      const auto& distribution_idx = _db.get_index< asset_distribution_index >().indices().get< by_symbol >();
      auto distribution_itr = distribution_idx.find( asset );
      if( distribution_itr != distribution_idx.end() )
      {
         results.back().distribution = distribution_api_obj( *distribution_itr );
      }

      const auto& balance_idx = _db.get_index< asset_distribution_balance_index >().indices().get< by_distribution_account >();
      auto balance_itr = balance_idx.find( asset );
      if( balance_itr != balance_idx.end() )
      {
         results.back().distribution_balances[ balance_itr->sender ] = distribution_balance_api_obj( *balance_itr );
      }

      const auto& fund_idx = _db.get_index< asset_reward_fund_index >().indices().get< by_symbol >();
      auto fund_itr = fund_idx.find( asset );
      if( fund_itr != fund_idx.end() )
      {
         results.back().reward_fund = reward_fund_api_obj( *fund_itr );
      }  
   }
   return results;
}


optional< escrow_api_obj > database_api::get_escrow( string from, string escrow_id )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_escrow( from, escrow_id );
   });
}


optional< escrow_api_obj > database_api_impl::get_escrow( string from, string escrow_id )const
{
   optional< escrow_api_obj > results;

   try
   {
      results = _db.get_escrow( from, escrow_id );
   }
   catch ( ... ) {}

   return results;
}


   //=====================//
   // === Communities === //
   //=====================//


vector< extended_community > database_api::get_communities( vector< string > communities )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_communities( communities );
   });
}

vector< extended_community > database_api_impl::get_communities( vector< string > communities )const
{
   vector< extended_community > results;
   const auto& community_idx = _db.get_index< community_index >().indices().get< by_name >();
   const auto& community_mem_idx = _db.get_index< community_member_index >().indices().get< by_name >();
   const auto& community_inv_idx = _db.get_index< community_join_invite_index >().indices().get< by_community >();
   const auto& community_req_idx = _db.get_index< community_join_request_index >().indices().get< by_community_account >();
   const auto& community_event_idx = _db.get_index< community_event_index >().indices().get< by_community_time >();

   for( string community : communities )
   {
      auto community_itr = community_idx.find( community );
      if( community_itr != community_idx.end() )
      results.push_back( extended_community( *community_itr ) );
      auto community_mem_itr = community_mem_idx.find( community );
      if( community_mem_itr != community_mem_idx.end() )
      {
         for( auto sub : community_mem_itr->subscribers )
         {
            results.back().subscribers.push_back( sub );
         }
         for( auto mem : community_mem_itr->members )
         {
            results.back().members.push_back( mem );
         }
         for( auto mod : community_mem_itr->moderators )
         {
            results.back().moderators.push_back( mod );
         }
         for( auto admin : community_mem_itr->administrators )
         {
            results.back().administrators.push_back( admin );
         }
         for( auto bl : community_mem_itr->blacklist )
         {
            results.back().blacklist.push_back( bl );
         }
         for( auto weight : community_mem_itr->mod_weight )
         {
            results.back().mod_weight[ weight.first ] = weight.second.value;
         }
         results.back().total_mod_weight = community_mem_itr->total_mod_weight.value;
      }
      
      auto community_event_itr = community_event_idx.lower_bound( community );
      while( community_event_itr != community_event_idx.end() && 
         community_event_itr->community == community )
      {
         results.back().events.push_back( community_event_api_obj( *community_event_itr ) );
         ++community_event_itr;
      }

      auto community_inv_itr = community_inv_idx.lower_bound( community );
      while( community_inv_itr != community_inv_idx.end() && community_inv_itr->community == community )
      {
         results.back().invites[ community_inv_itr->member ] = community_invite_api_obj( *community_inv_itr );
         ++community_inv_itr;
      }

      auto community_req_itr = community_req_idx.lower_bound( community );

      while( community_req_itr != community_req_idx.end() && community_req_itr->community == community )
      {
         results.back().requests[ community_inv_itr->account ] = community_request_api_obj( *community_req_itr );
         ++community_req_itr;
      }
   }
   return results;
}

vector< extended_community > database_api::get_communities_by_subscribers( string from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_communities_by_subscribers( from, limit );
   });
}

vector< extended_community > database_api_impl::get_communities_by_subscribers( string from, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 1000 ) );
   vector< extended_community > results;
   results.reserve( limit );

   const auto& community_idx = _db.get_index< community_index >().indices().get< by_subscriber_count >();
   const auto& name_idx = _db.get_index< community_index >().indices().get< by_name >();
   const auto& community_mem_idx = _db.get_index< community_member_index >().indices().get< by_name >();
   const auto& community_inv_idx = _db.get_index< community_join_invite_index >().indices().get< by_community >();
   const auto& community_req_idx = _db.get_index< community_join_request_index >().indices().get< by_community_account >();
   const auto& community_event_idx = _db.get_index< community_event_index >().indices().get< by_community_time >();

   auto community_itr = community_idx.begin();
  
   if( from.size() )
   {
      auto name_itr = name_idx.find( from );
      FC_ASSERT( name_itr != name_idx.end(),
         "Invalid Community name ${n}", ("n",from) );
      community_itr = community_idx.iterator_to( *name_itr );
   }

   while( community_itr != community_idx.end() && results.size() < limit )
   {
      results.push_back( extended_community( *community_itr ) );
      community_name_type community = community_itr->name;
      auto community_mem_itr = community_mem_idx.find( community );
      if( community_mem_itr != community_mem_idx.end() )
      {
         for( auto sub : community_mem_itr->subscribers )
         {
            results.back().subscribers.push_back( sub );
         }
         for( auto mem : community_mem_itr->members )
         {
            results.back().members.push_back( mem );
         }
         for( auto mod : community_mem_itr->moderators )
         {
            results.back().moderators.push_back( mod );
         }
         for( auto admin : community_mem_itr->administrators )
         {
            results.back().administrators.push_back( admin );
         }
         for( auto bl : community_mem_itr->blacklist )
         {
            results.back().blacklist.push_back( bl );
         }
         for( auto weight : community_mem_itr->mod_weight )
         {
            results.back().mod_weight[ weight.first ] = weight.second.value;
         }
         results.back().total_mod_weight = community_mem_itr->total_mod_weight.value;
      }

      auto community_event_itr = community_event_idx.lower_bound( community );
      while( community_event_itr != community_event_idx.end() && 
         community_event_itr->community == community )
      {
         results.back().events.push_back( community_event_api_obj( *community_event_itr ) );
         ++community_event_itr;
      }

      auto community_inv_itr = community_inv_idx.lower_bound( community );
      while( community_inv_itr != community_inv_idx.end() && 
         community_inv_itr->community == community )
      {
         results.back().invites[ community_inv_itr->member ] = community_invite_api_obj( *community_inv_itr );
         ++community_inv_itr;
      }

      auto community_req_itr = community_req_idx.lower_bound( community );
      while( community_req_itr != community_req_idx.end() && 
         community_req_itr->community == community )
      {
         results.back().requests[ community_inv_itr->account ] = community_request_api_obj( *community_req_itr );
         ++community_req_itr;
      }
   }
   return results;
}


   //=================//
   // === Network === //
   //=================//


vector< account_network_state > database_api::get_account_network_state( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_account_network_state( names );
   });
}

vector< account_network_state > database_api_impl::get_account_network_state( vector< string > names )const
{
   vector< account_network_state > results;

   results.reserve( names.size() );

   const auto& producer_idx = _db.get_index< producer_index >().indices().get< by_name >();
   const auto& executive_idx = _db.get_index< executive_board_index >().indices().get< by_account >();
   const auto& officer_idx = _db.get_index< network_officer_index >().indices().get< by_account >();
   const auto& enterprise_idx = _db.get_index< enterprise_index >().indices().get< by_enterprise_id >();
   const auto& interface_idx = _db.get_index< interface_index >().indices().get< by_account >();
   const auto& supernode_idx = _db.get_index< supernode_index >().indices().get< by_account >();
   const auto& governance_idx = _db.get_index< governance_account_index >().indices().get< by_account >();
   const auto& validation_idx = _db.get_index< block_validation_index >().indices().get< by_producer_height >();
   
   const auto& incoming_producer_vote_idx = _db.get_index< producer_vote_index >().indices().get< by_producer_account >();
   const auto& incoming_executive_vote_idx = _db.get_index< executive_board_vote_index >().indices().get< by_executive_account >();
   const auto& incoming_officer_vote_idx = _db.get_index< network_officer_vote_index >().indices().get< by_officer_account >();
   const auto& incoming_subscription_idx = _db.get_index< governance_subscription_index >().indices().get< by_governance_account >();
   const auto& incoming_enterprise_vote_idx = _db.get_index< enterprise_vote_index >().indices().get< by_enterprise_id >();
   const auto& incoming_commit_violation_idx = _db.get_index< commit_violation_index >().indices().get< by_producer_height >();

   const auto& outgoing_producer_vote_idx = _db.get_index< producer_vote_index >().indices().get< by_account_rank >();
   const auto& outgoing_executive_vote_idx = _db.get_index< executive_board_vote_index >().indices().get< by_account_rank >();
   const auto& outgoing_officer_vote_idx = _db.get_index< network_officer_vote_index >().indices().get< by_account_type_rank >();
   const auto& outgoing_subscription_idx = _db.get_index< governance_subscription_index >().indices().get< by_account_rank >();
   const auto& outgoing_enterprise_vote_idx = _db.get_index< enterprise_vote_index >().indices().get< by_account_rank >();
   const auto& outgoing_commit_violation_idx = _db.get_index< commit_violation_index >().indices().get< by_reporter_height >();

   for( auto name : names )
   {
      account_network_state nstate;

      auto producer_itr = producer_idx.find( name );
      if( producer_itr != producer_idx.end() )
      {
         nstate.producer = producer_api_obj( *producer_itr );
      }

      auto officer_itr = officer_idx.find( name );
      if( officer_itr != officer_idx.end() )
      {
         nstate.network_officer = network_officer_api_obj( *officer_itr );
      }

      auto executive_itr = executive_idx.find( name );
      if( executive_itr != executive_idx.end() )
      {
         nstate.executive_board = executive_board_api_obj( *executive_itr );
      } 

      auto interface_itr = interface_idx.find( name );
      if( interface_itr != interface_idx.end() )
      {
         nstate.interface = interface_api_obj( *interface_itr );
      }

      auto supernode_itr = supernode_idx.find( name );
      if( supernode_itr != supernode_idx.end() )
      {
         nstate.supernode = supernode_api_obj( *supernode_itr );
      }

      auto governance_itr = governance_idx.find( name );
      if( governance_itr != governance_idx.end() )
      {
         nstate.governance_account = governance_account_api_obj( *governance_itr );
      }

      auto enterprise_itr = enterprise_idx.lower_bound( name );
      while( enterprise_itr != enterprise_idx.end() && enterprise_itr->account == name )
      {
         nstate.enterprise_proposals.push_back( enterprise_api_obj( *enterprise_itr ) );
         ++enterprise_itr;
      }

      auto validation_itr = validation_idx.lower_bound( name );
      while( validation_itr != validation_idx.end() && 
         validation_itr->producer == name &&
         nstate.block_validations.size() < 100 )
      {
         nstate.block_validations.push_back( block_validation_api_obj( *validation_itr ) );
         ++validation_itr;
      }

      auto incoming_producer_vote_itr = incoming_producer_vote_idx.lower_bound( name );
      while( incoming_producer_vote_itr != incoming_producer_vote_idx.end() && 
         incoming_producer_vote_itr->producer == name )
      {
         nstate.incoming_producer_votes[ incoming_producer_vote_itr->account ] = producer_vote_api_obj( *incoming_producer_vote_itr );
         ++incoming_producer_vote_itr;
      }

      auto incoming_officer_vote_itr = incoming_officer_vote_idx.lower_bound( name );
      while( incoming_officer_vote_itr != incoming_officer_vote_idx.end() && 
         incoming_officer_vote_itr->network_officer == name )
      {
         nstate.incoming_network_officer_votes[ incoming_officer_vote_itr->account ] = network_officer_vote_api_obj( *incoming_officer_vote_itr );
         ++incoming_officer_vote_itr;
      }

      auto incoming_executive_vote_itr = incoming_executive_vote_idx.lower_bound( name );
      while( incoming_executive_vote_itr != incoming_executive_vote_idx.end() && 
         incoming_executive_vote_itr->executive_board == name )
      {
         nstate.incoming_executive_board_votes[ incoming_executive_vote_itr->account ] = executive_board_vote_api_obj( *incoming_executive_vote_itr );
         ++incoming_executive_vote_itr;
      }

      auto incoming_subscription_itr = incoming_subscription_idx.lower_bound( name );
      while( incoming_subscription_itr != incoming_subscription_idx.end() && 
         incoming_subscription_itr->governance_account == name )
      {
         nstate.incoming_governance_subscriptions[ incoming_subscription_itr->account ] = governance_subscription_api_obj( *incoming_subscription_itr );
         ++incoming_subscription_itr;
      }

      auto incoming_enterprise_vote_itr = incoming_enterprise_vote_idx.lower_bound( name );
      while( incoming_enterprise_vote_itr != incoming_enterprise_vote_idx.end() && 
         incoming_enterprise_vote_itr->account == name )
      {
         nstate.incoming_enterprise_votes[ incoming_enterprise_vote_itr->account ][ to_string( incoming_enterprise_vote_itr->enterprise_id ) ] = enterprise_vote_api_obj( *incoming_enterprise_vote_itr );
         ++incoming_enterprise_vote_itr;
      }

      auto incoming_commit_violation_itr = incoming_commit_violation_idx.lower_bound( name );
      while( incoming_commit_violation_itr != incoming_commit_violation_idx.end() && 
         incoming_commit_violation_itr->producer == name &&
         nstate.incoming_commit_violations.size() < 100 )
      {
         nstate.incoming_commit_violations[ incoming_commit_violation_itr->reporter ] = commit_violation_api_obj( *incoming_commit_violation_itr );
         ++incoming_commit_violation_itr;
      }

      auto outgoing_producer_vote_itr = outgoing_producer_vote_idx.lower_bound( name );
      while( outgoing_producer_vote_itr != outgoing_producer_vote_idx.end() && 
         outgoing_producer_vote_itr->account == name ) 
      {
         nstate.outgoing_producer_votes[ outgoing_producer_vote_itr->producer ] = producer_vote_api_obj( *outgoing_producer_vote_itr );
         ++outgoing_producer_vote_itr;
      }

      auto outgoing_officer_vote_itr = outgoing_officer_vote_idx.lower_bound( name );
      while( outgoing_officer_vote_itr != outgoing_officer_vote_idx.end() && 
         outgoing_officer_vote_itr->account == name )
      {
         nstate.outgoing_network_officer_votes[ outgoing_officer_vote_itr->network_officer ] = network_officer_vote_api_obj( *outgoing_officer_vote_itr );
         ++outgoing_officer_vote_itr;
      }

      auto outgoing_executive_vote_itr = outgoing_executive_vote_idx.lower_bound( name );
      while( outgoing_executive_vote_itr != outgoing_executive_vote_idx.end() && 
         outgoing_executive_vote_itr->account == name )
      {
         nstate.outgoing_executive_board_votes[ outgoing_executive_vote_itr->executive_board ] = executive_board_vote_api_obj( *outgoing_executive_vote_itr );
         ++outgoing_executive_vote_itr;
      }

      auto outgoing_subscription_itr = outgoing_subscription_idx.lower_bound( name );
      while( outgoing_subscription_itr != outgoing_subscription_idx.end() && 
         outgoing_subscription_itr->account == name )
      {
         nstate.outgoing_governance_subscriptions[ outgoing_subscription_itr->governance_account ] = governance_subscription_api_obj( *outgoing_subscription_itr );
         ++outgoing_subscription_itr;
      }

      auto outgoing_enterprise_vote_itr = outgoing_enterprise_vote_idx.lower_bound( name );
      while( outgoing_enterprise_vote_itr != outgoing_enterprise_vote_idx.end() && 
         outgoing_enterprise_vote_itr->account == name )
      {
         nstate.outgoing_enterprise_votes[ outgoing_enterprise_vote_itr->account ][ to_string( outgoing_enterprise_vote_itr->enterprise_id ) ] = enterprise_vote_api_obj( *outgoing_enterprise_vote_itr );
         ++outgoing_enterprise_vote_itr;
      }

      auto outgoing_commit_violation_itr = outgoing_commit_violation_idx.lower_bound( name );
      while( outgoing_commit_violation_itr != outgoing_commit_violation_idx.end() && 
         outgoing_commit_violation_itr->reporter == name &&
         nstate.outgoing_commit_violations.size() < 100 )
      {
         nstate.outgoing_commit_violations[ outgoing_commit_violation_itr->producer ] = commit_violation_api_obj( *outgoing_commit_violation_itr );
         ++outgoing_commit_violation_itr;
      }

      results.push_back( nstate );
   }

   return results;
}

vector< account_name_type > database_api::get_active_producers()const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_active_producers();
   });
}

vector< account_name_type > database_api_impl::get_active_producers()const
{
   const producer_schedule_object& pso = _db.get_producer_schedule();
   size_t n = pso.current_shuffled_producers.size();
   vector< account_name_type > results;
   results.reserve( n );
   
   for( size_t i=0; i<n; i++ )
   {
      results.push_back( pso.current_shuffled_producers[ i ] );
   }
      
   return results;
}

vector< producer_api_obj > database_api::get_producers_by_voting_power( string from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_producers_by_voting_power( from, limit );
   });
}

vector< producer_api_obj > database_api_impl::get_producers_by_voting_power( string from, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 1000 ) );
   vector< producer_api_obj > results;
   results.reserve( limit );

   const auto& name_idx = _db.get_index< producer_index >().indices().get< by_name >();
   const auto& vote_idx = _db.get_index< producer_index >().indices().get< by_voting_power >();

   auto vote_itr = vote_idx.begin();
   if( from.size() ) 
   {
      auto name_itr = name_idx.find( from );
      FC_ASSERT( name_itr != name_idx.end(),
         "Invalid producer name ${n}", ("n",from) );
      vote_itr = vote_idx.iterator_to( *name_itr );
   }

   while( vote_itr != vote_idx.end() && results.size() < limit && vote_itr->vote_count > 0 )
   {
      results.push_back( producer_api_obj( *vote_itr ) );
      ++vote_itr;
   }
   return results;
}

vector< producer_api_obj > database_api::get_producers_by_mining_power( string from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_producers_by_mining_power( from, limit );
   });
}

vector< producer_api_obj > database_api_impl::get_producers_by_mining_power( string from, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 1000 ) );
   vector< producer_api_obj > results;
   results.reserve( limit );

   const auto& name_idx = _db.get_index< producer_index >().indices().get< by_name >();
   const auto& mining_idx = _db.get_index< producer_index >().indices().get< by_mining_power >();

   auto mining_itr = mining_idx.begin();
   if( from.size() ) 
   {
      auto name_itr = name_idx.find( from );
      FC_ASSERT( name_itr != name_idx.end(),
         "Invalid producer name ${n}", ("n",from) );
      mining_itr = mining_idx.iterator_to( *name_itr );
   }

   while( mining_itr != mining_idx.end() &&
      results.size() < limit && mining_itr->mining_count > 0 )
   {
      results.push_back( producer_api_obj( *mining_itr ) );
      ++mining_itr;
   }
   return results;
}


vector< network_officer_api_obj > database_api::get_development_officers_by_voting_power( string currency, string from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_development_officers_by_voting_power( currency, from, limit );
   });
}

vector< network_officer_api_obj > database_api_impl::get_development_officers_by_voting_power( string currency, string from, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 1000 ) );
   vector< network_officer_api_obj > results;
   results.reserve( limit );

   const auto& name_idx = _db.get_index< network_officer_index >().indices().get< by_account >();
   const auto& vote_idx = _db.get_index< network_officer_index >().indices().get< by_symbol_type_voting_power >();

   auto vote_itr = vote_idx.lower_bound( boost::make_tuple( currency, network_officer_role_type::DEVELOPMENT ) );

   if( from.size() ) 
   {
      auto name_itr = name_idx.find( from );
      FC_ASSERT( name_itr != name_idx.end(),
         "Invalid network officer name ${n}", ("n",from) );
      vote_itr = vote_idx.iterator_to( *name_itr );
   }

   while( vote_itr != vote_idx.end() && 
      results.size() < limit && 
      vote_itr->vote_count > 0 && 
      vote_itr->officer_type == network_officer_role_type::DEVELOPMENT )
   {
      results.push_back( network_officer_api_obj( *vote_itr ) );
      ++vote_itr;
   }
   return results;
}

vector< network_officer_api_obj > database_api::get_marketing_officers_by_voting_power( string currency, string from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_marketing_officers_by_voting_power( currency, from, limit );
   });
}

vector< network_officer_api_obj > database_api_impl::get_marketing_officers_by_voting_power( string currency, string from, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 1000 ) );
   vector< network_officer_api_obj > results;
   results.reserve( limit );

   const auto& name_idx = _db.get_index< network_officer_index >().indices().get< by_account >();
   const auto& vote_idx = _db.get_index< network_officer_index >().indices().get< by_symbol_type_voting_power >();

   auto vote_itr = vote_idx.lower_bound( boost::make_tuple( currency, network_officer_role_type::MARKETING ) );

   if( from.size() ) 
   {
      auto name_itr = name_idx.find( from );
      FC_ASSERT( name_itr != name_idx.end(), "Invalid network officer name ${n}", ("n",from) );
      vote_itr = vote_idx.iterator_to( *name_itr );
   }

   while( vote_itr != vote_idx.end() && results.size() < limit && vote_itr->vote_count > 0 && vote_itr->officer_type == network_officer_role_type::MARKETING )
   {
      results.push_back( network_officer_api_obj( *vote_itr ) );
      ++vote_itr;
   }
   return results;
}

vector< network_officer_api_obj > database_api::get_advocacy_officers_by_voting_power( string currency, string from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_advocacy_officers_by_voting_power( currency, from, limit );
   });
}

vector< network_officer_api_obj > database_api_impl::get_advocacy_officers_by_voting_power( string currency, string from, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 1000 ) );
   vector< network_officer_api_obj > results;
   results.reserve( limit );

   const auto& name_idx = _db.get_index< network_officer_index >().indices().get< by_account >();
   const auto& vote_idx = _db.get_index< network_officer_index >().indices().get< by_symbol_type_voting_power >();

   auto vote_itr = vote_idx.lower_bound( boost::make_tuple( currency, network_officer_role_type::ADVOCACY ) );

   if( from.size() ) 
   {
      auto name_itr = name_idx.find( from );
      FC_ASSERT( name_itr != name_idx.end(),
         "Invalid network officer name ${n}", ("n",from) );
      vote_itr = vote_idx.iterator_to( *name_itr );
   }

   while( vote_itr != vote_idx.end() && 
      results.size() < limit && 
      vote_itr->vote_count > 0 && 
      vote_itr->officer_type == network_officer_role_type::ADVOCACY )
   {
      results.push_back( network_officer_api_obj( *vote_itr ) );
      ++vote_itr;
   }
   return results;
}

vector< executive_board_api_obj > database_api::get_executive_boards_by_voting_power( string from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_executive_boards_by_voting_power( from, limit );
   });
}

vector< executive_board_api_obj > database_api_impl::get_executive_boards_by_voting_power( string from, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 1000 ) );
   vector< executive_board_api_obj > results;
   results.reserve( limit );

   const auto& name_idx = _db.get_index< executive_board_index >().indices().get< by_account >();
   const auto& vote_idx = _db.get_index< executive_board_index >().indices().get< by_voting_power >();

   auto vote_itr = vote_idx.begin();
   if( from.size() ) 
   {
      auto name_itr = name_idx.find( from );
      FC_ASSERT( name_itr != name_idx.end(),
         "Invalid executive board name ${n}", ("n",from) );
      vote_itr = vote_idx.iterator_to( *name_itr );
   }

   while( vote_itr != vote_idx.end() && 
      results.size() < limit && 
      vote_itr->vote_count > 0 )
   {
      results.push_back( executive_board_api_obj( *vote_itr ) );
      ++vote_itr;
   }
   return results;
}

vector< supernode_api_obj > database_api::get_supernodes_by_view_weight( string from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_supernodes_by_view_weight( from, limit );
   });
}

vector< supernode_api_obj > database_api_impl::get_supernodes_by_view_weight( string from, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 1000 ) );
   vector< supernode_api_obj > results;
   results.reserve( limit );

   const auto& name_idx = _db.get_index< supernode_index >().indices().get< by_account >();
   const auto& view_idx = _db.get_index< supernode_index >().indices().get< by_view_weight >();

   auto view_itr = view_idx.begin();
   if( from.size() ) 
   {
      auto name_itr = name_idx.find( from );
      FC_ASSERT( name_itr != name_idx.end(),
         "Invalid supernode name ${n}", ("n",from) );
      view_itr = view_idx.iterator_to( *name_itr );
   }

   while( view_itr != view_idx.end() && 
      results.size() < limit && 
      view_itr->monthly_active_users > 0 )
   {
      results.push_back( supernode_api_obj( *view_itr ) );
      ++view_itr;
   }
   return results;
}

vector< interface_api_obj > database_api::get_interfaces_by_users( string from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_interfaces_by_users( from, limit );
   });
}

vector< interface_api_obj > database_api_impl::get_interfaces_by_users( string from, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 1000 ) );

   vector< interface_api_obj > results;
   results.reserve( limit );

   const auto& name_idx = _db.get_index< interface_index >().indices().get< by_account >();
   const auto& user_idx = _db.get_index< interface_index >().indices().get< by_monthly_active_users >();

   auto user_itr = user_idx.begin();
   if( from.size() ) 
   {
      auto name_itr = name_idx.find( from );
      FC_ASSERT( name_itr != name_idx.end(),
         "Invalid interface name ${n}", ("n",from) );
      user_itr = user_idx.iterator_to( *name_itr );
   }

   while( user_itr != user_idx.end() && 
      results.size() < limit && 
      user_itr->monthly_active_users > 0 )
   {
      results.push_back( interface_api_obj( *user_itr ) );
      ++user_itr;
   }
   return results;
}

vector< governance_account_api_obj > database_api::get_governance_accounts_by_subscriber_power( string from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_governance_accounts_by_subscriber_power( from, limit );
   });
}

vector< governance_account_api_obj > database_api_impl::get_governance_accounts_by_subscriber_power( string from, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 1000 ) );

   vector< governance_account_api_obj > results;
   results.reserve( limit );

   const auto& name_idx = _db.get_index< governance_account_index >().indices().get< by_account >();
   const auto& sub_idx = _db.get_index< governance_account_index >().indices().get< by_subscriber_power >();

   auto sub_itr = sub_idx.begin();
   if( from.size() ) 
   {
      auto name_itr = name_idx.find( from );
      FC_ASSERT( name_itr != name_idx.end(),
         "Invalid governance account name ${n}", ("n",from) );
      sub_itr = sub_idx.iterator_to( *name_itr );
   }

   while( sub_itr != sub_idx.end() && 
      results.size() < limit && 
      sub_itr->subscriber_count > 0 )
   {
      results.push_back( governance_account_api_obj( *sub_itr ) );
      ++sub_itr;
   }
   return results;
}

vector< enterprise_api_obj > database_api::get_enterprise_by_voting_power( string from, string from_id, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_enterprise_by_voting_power( from, from_id, limit );
   });
}

vector< enterprise_api_obj > database_api_impl::get_enterprise_by_voting_power( string from, string from_id, uint32_t limit )const
{
   limit = std::min( limit, uint32_t( 1000 ) );

   vector< enterprise_api_obj > results;
   results.reserve( limit );

   const auto& name_idx = _db.get_index< enterprise_index >().indices().get< by_enterprise_id >();
   const auto& vote_idx = _db.get_index< enterprise_index >().indices().get< by_total_voting_power >();

   auto vote_itr = vote_idx.begin();
   if( from.size() ) 
   {
      auto name_itr = name_idx.find( boost::make_tuple( from, from_id ) );
      FC_ASSERT( name_itr != name_idx.end(),
         "Invalid enterprise Creator: ${c} with enterprise_id: ${i}", ("c",from)("i",from_id) );
      vote_itr = vote_idx.iterator_to( *name_itr );
   }

   while( vote_itr != vote_idx.end() && 
      results.size() < limit && 
      vote_itr->vote_count > 0 )
   {
      results.push_back( enterprise_api_obj( *vote_itr ) );
      ++vote_itr;
   }
   return results;
}


   //================//
   // === Market === //
   //================//


vector< order_state > database_api::get_open_orders( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_open_orders( names );
   });
}

vector< order_state > database_api_impl::get_open_orders( vector< string > names )const
{
   vector< order_state > results;
   const auto& limit_idx = _db.get_index< limit_order_index >().indices().get< by_account >();
   const auto& margin_idx = _db.get_index< margin_order_index >().indices().get< by_account >();
   const auto& call_idx = _db.get_index< call_order_index >().indices().get< by_account >();
   const auto& loan_idx = _db.get_index< credit_loan_index >().indices().get< by_owner >();
   const auto& collateral_idx = _db.get_index< credit_collateral_index >().indices().get< by_owner >();

   for( auto name : names )
   {
      order_state ostate;
      auto limit_itr = limit_idx.lower_bound( name );
      while( limit_itr != limit_idx.end() && limit_itr->seller == name ) 
      {
         ostate.limit_orders.push_back( limit_order_api_obj( *limit_itr ) );
         ++limit_itr;
      }

      auto margin_itr = margin_idx.lower_bound( name );
      while( margin_itr != margin_idx.end() && margin_itr->owner == name ) 
      {
         ostate.margin_orders.push_back( margin_order_api_obj( *margin_itr ) );
         ++margin_itr;
      }

      auto call_itr = call_idx.lower_bound( name );
      while( call_itr != call_idx.end() && call_itr->borrower == name ) 
      {
         ostate.call_orders.push_back( call_order_api_obj( *call_itr ) );
         ++call_itr;
      }

      auto loan_itr = loan_idx.lower_bound( name );
      while( loan_itr != loan_idx.end() && loan_itr->owner == name ) 
      {
         ostate.loan_orders.push_back( credit_loan_api_obj( *loan_itr ) );
         ++loan_itr;
      }

      auto collateral_itr = collateral_idx.lower_bound( name );
      while( collateral_itr != collateral_idx.end() && collateral_itr->owner == name ) 
      {
         ostate.collateral.push_back( credit_collateral_api_obj( *collateral_itr ) );
         ++collateral_itr;
      }
   }
   return results;
}

market_limit_orders database_api::get_limit_orders( string buy_symbol, string sell_symbol, uint32_t limit = 1000 )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_limit_orders( buy_symbol, sell_symbol, limit );
   });
}

market_limit_orders database_api_impl::get_limit_orders( string buy_symbol, string sell_symbol, uint32_t limit = 1000 )const
{
   FC_ASSERT( buy_symbol != sell_symbol, 
      "Buy Symbol cannot be equal to be Sell symbol." );
   limit = std::min( limit, uint32_t( 1000 ) );

   market_limit_orders results;

   const auto& limit_price_idx = _db.get_index< limit_order_index >().indices().get< by_high_price >();

   auto max_sell = price::max( asset_symbol_type( sell_symbol ), asset_symbol_type( buy_symbol ) );
   auto max_buy = price::max( asset_symbol_type( buy_symbol ), asset_symbol_type( sell_symbol ) );
   
   auto limit_sell_itr = limit_price_idx.lower_bound( max_sell );
   auto limit_buy_itr = limit_price_idx.lower_bound( max_buy );
   auto limit_end = limit_price_idx.end();

   while( limit_sell_itr != limit_end &&
      limit_sell_itr->sell_price.base.symbol == asset_symbol_type( sell_symbol ) && 
      results.limit_bids.size() < limit )
   {
      results.limit_bids.push_back( limit_order_api_obj( *limit_sell_itr ) );
      ++limit_sell_itr;
   }
   while( limit_buy_itr != limit_end && 
      limit_buy_itr->sell_price.base.symbol == asset_symbol_type( buy_symbol ) && 
      results.limit_asks.size() < limit )
   {
      results.limit_asks.push_back( limit_order_api_obj( *limit_buy_itr ) );
      ++limit_buy_itr;  
   }
   return results;
}

market_margin_orders database_api::get_margin_orders( string buy_symbol, string sell_symbol, uint32_t limit = 1000 )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_margin_orders( buy_symbol, sell_symbol, limit );
   });
}

market_margin_orders database_api_impl::get_margin_orders( string buy_symbol, string sell_symbol, uint32_t limit = 1000 )const
{
   FC_ASSERT( buy_symbol != sell_symbol, 
      "Buy Symbol cannot be equal to be Sell symbol." );
   limit = std::min( limit, uint32_t( 1000 ) );

   market_margin_orders results;

   const auto& margin_price_idx = _db.get_index< margin_order_index >().indices().get< by_high_price >();

   auto max_sell = price::max( asset_symbol_type( sell_symbol ), asset_symbol_type( buy_symbol ) );
   auto max_buy = price::max( asset_symbol_type( buy_symbol ), asset_symbol_type( sell_symbol ) );
   
   auto margin_sell_itr = margin_price_idx.lower_bound( boost::make_tuple( false, max_sell ) );
   auto margin_buy_itr = margin_price_idx.lower_bound( boost::make_tuple( false, max_buy ) );
   auto margin_end = margin_price_idx.end();

   while( margin_sell_itr != margin_end &&
      margin_sell_itr->sell_price.base.symbol == asset_symbol_type( sell_symbol ) &&
      results.margin_bids.size() < limit )
   {
      results.margin_bids.push_back( margin_order_api_obj( *margin_sell_itr ) );
      ++margin_sell_itr;
   }
   while( margin_buy_itr != margin_end && 
      margin_buy_itr->sell_price.base.symbol == asset_symbol_type( buy_symbol ) &&
      results.margin_asks.size() < limit )
   {
      results.margin_asks.push_back( margin_order_api_obj( *margin_buy_itr ) );
      ++margin_buy_itr;  
   }
   return results;
}

market_option_orders database_api::get_option_orders( string buy_symbol, string sell_symbol, uint32_t limit = 1000 )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_option_orders( buy_symbol, sell_symbol, limit );
   });
}

market_option_orders database_api_impl::get_option_orders( string buy_symbol, string sell_symbol, uint32_t limit = 1000 )const
{
   FC_ASSERT( buy_symbol != sell_symbol, 
      "Buy Symbol cannot be equal to be Sell symbol." );
   limit = std::min( limit, uint32_t( 1000 ) );

   market_option_orders results;

   const asset_object& buy_asset = _db.get_asset( asset_symbol_type( buy_symbol ) );
   const asset_object& sell_asset = _db.get_asset( asset_symbol_type( sell_symbol ) );
   asset_symbol_type symbol_a;
   asset_symbol_type symbol_b;

   if( buy_asset.id < sell_asset.id )
   {
      symbol_a = buy_asset.symbol;
      symbol_b = sell_asset.symbol;
   }
   else
   {
      symbol_b = buy_asset.symbol;
      symbol_a = sell_asset.symbol;
   }

   const auto& option_idx = _db.get_index< option_order_index >().indices().get< by_high_price >();

   auto max_price = price::max( asset_symbol_type( symbol_a ), asset_symbol_type( symbol_b ) );
   auto option_itr = option_idx.lower_bound( max_price );
   auto option_end = option_idx.end();

   while( option_itr != option_end &&
      option_itr->option_price().base.symbol == asset_symbol_type( symbol_a ) &&
      option_itr->option_price().quote.symbol == asset_symbol_type( symbol_b ) &&
      results.option_calls.size() < limit &&
      results.option_puts.size() < limit )
   {
      if( option_itr->call() )
      {
         results.option_calls.push_back( option_order_api_obj( *option_itr ) );
      }
      else
      {
         results.option_puts.push_back( option_order_api_obj( *option_itr ) );
      }
      
      ++option_itr;
   }
   
   return results;
}

market_call_orders database_api::get_call_orders( string buy_symbol, string sell_symbol, uint32_t limit = 1000 )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_call_orders( buy_symbol, sell_symbol, limit );
   });
}

market_call_orders database_api_impl::get_call_orders( string buy_symbol, string sell_symbol, uint32_t limit = 1000 )const
{
   FC_ASSERT( buy_symbol != sell_symbol, 
      "Buy Symbol cannot be equal to be Sell symbol." );
   limit = std::min( limit, uint32_t( 1000 ) );

   market_call_orders results;

   const asset_object& buy_asset = _db.get_asset( buy_symbol );
   const asset_object& sell_asset = _db.get_asset( sell_symbol );

   asset_symbol_type stablecoin_symbol;

   if( buy_asset.asset_type == asset_property_type::STABLECOIN_ASSET )
   {
      stablecoin_symbol = buy_asset.symbol;
   }
   else if( sell_asset.asset_type == asset_property_type::STABLECOIN_ASSET )
   {
      stablecoin_symbol = sell_asset.symbol;
   }
   else
   {
      return results;
   }

   const asset_stablecoin_data_object& bit_obj = _db.get_stablecoin_data( stablecoin_symbol );
   results.settlement_price = bit_obj.current_feed.settlement_price;

   const auto& call_idx = _db.get_index< call_order_index >().indices().get< by_debt >();
   
   auto call_itr = call_idx.lower_bound( stablecoin_symbol );
   auto call_end = call_idx.end();

   while( call_itr != call_end &&
      call_itr->debt_type() == stablecoin_symbol && 
      results.calls.size() < limit )
   {
      results.calls.push_back( call_order_api_obj( *call_itr ) );
      ++call_itr;
   }
   
   return results;
}

market_auction_orders database_api::get_auction_orders( string buy_symbol, string sell_symbol, uint32_t limit = 1000 )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_auction_orders( buy_symbol, sell_symbol, limit );
   });
}

market_auction_orders database_api_impl::get_auction_orders( string buy_symbol, string sell_symbol, uint32_t limit = 1000 )const
{
   FC_ASSERT( buy_symbol != sell_symbol, 
      "Buy Symbol cannot be equal to be Sell symbol." );
   limit = std::min( limit, uint32_t( 1000 ) );

   market_auction_orders results;

   const auto& auction_price_idx = _db.get_index< auction_order_index >().indices().get< by_high_price >();

   auto max_sell = price::max( asset_symbol_type( sell_symbol ), asset_symbol_type( buy_symbol ) );
   auto max_buy = price::max( asset_symbol_type( buy_symbol ), asset_symbol_type( sell_symbol ) );
   
   auto auction_sell_itr = auction_price_idx.lower_bound( max_sell );
   auto auction_buy_itr = auction_price_idx.lower_bound( max_buy );
   auto auction_end = auction_price_idx.end();

   while( auction_sell_itr != auction_end &&
      auction_sell_itr->sell_asset() == asset_symbol_type( sell_symbol ) &&
      results.product_auction_bids.size() < limit )
   {
      results.product_auction_bids.push_back( auction_order_api_obj( *auction_sell_itr ) );
      ++auction_sell_itr;
   }
   while( auction_buy_itr != auction_end && 
      auction_buy_itr->sell_asset() == asset_symbol_type( buy_symbol ) &&
      results.auction_asks.size() < limit )
   {
      results.auction_asks.push_back( auction_order_api_obj( *auction_buy_itr ) );
      ++auction_buy_itr;  
   }
   return results;
}

market_credit_loans database_api::get_credit_loans( string buy_symbol, string sell_symbol, uint32_t limit = 1000 )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_credit_loans( buy_symbol, sell_symbol, limit );
   });
}

market_credit_loans database_api_impl::get_credit_loans( string buy_symbol, string sell_symbol, uint32_t limit = 1000 )const
{
   FC_ASSERT( buy_symbol != sell_symbol, 
      "Buy Symbol cannot be equal to be Sell symbol." );
   limit = std::min( limit, uint32_t( 1000 ) );
   
   market_credit_loans results;

   const auto& loan_idx = _db.get_index<credit_loan_index>().indices().get< by_liquidation_spread >();
   
   auto loan_buy_itr = loan_idx.lower_bound( boost::make_tuple( asset_symbol_type( buy_symbol ), asset_symbol_type( sell_symbol ) ) );
   auto loan_sell_itr = loan_idx.lower_bound( boost::make_tuple( asset_symbol_type( sell_symbol ), asset_symbol_type( buy_symbol ) ) );
   auto loan_end = loan_idx.end();

   while( loan_sell_itr != loan_end &&
      loan_sell_itr->debt_asset() == asset_symbol_type( sell_symbol ) &&
      results.loan_bids.size() < limit )
   {
      results.loan_bids.push_back( credit_loan_api_obj( *loan_sell_itr ) );
      ++loan_sell_itr;
   }
   while( loan_buy_itr != loan_end &&
      loan_buy_itr->debt_asset() == asset_symbol_type( buy_symbol ) &&
      results.loan_asks.size() < limit )
   {
      results.loan_asks.push_back( credit_loan_api_obj( *loan_buy_itr ) );
      ++loan_buy_itr;
   }
   return results;
}

vector< credit_pool_api_obj> database_api::get_credit_pools( vector< string > assets )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_credit_pools( assets );
   });
}

vector< credit_pool_api_obj> database_api_impl::get_credit_pools( vector< string > assets )const
{
   vector< credit_pool_api_obj> results;

   const auto& pool_idx = _db.get_index< asset_credit_pool_index >().indices().get< by_base_symbol >();

   for( auto symbol : assets )
   {
      auto pool_itr = pool_idx.find( symbol );
      if( pool_itr != pool_idx.end() )
      {
         results.push_back( credit_pool_api_obj( *pool_itr ) );
      }
   }

   return results; 
}

vector< liquidity_pool_api_obj > database_api::get_liquidity_pools( string buy_symbol, string sell_symbol )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_liquidity_pools( buy_symbol, sell_symbol );
   });
}

vector< liquidity_pool_api_obj > database_api_impl::get_liquidity_pools( string buy_symbol, string sell_symbol )const
{
   FC_ASSERT( buy_symbol != sell_symbol, 
      "Buy Symbol cannot be equal to be Sell symbol." );

   vector< liquidity_pool_api_obj > results;

   const asset_object& buy_asset = _db.get_asset( asset_symbol_type( buy_symbol ) );
   const asset_object& sell_asset = _db.get_asset( asset_symbol_type( sell_symbol ) );
   asset_symbol_type symbol_a;
   asset_symbol_type symbol_b;

   if( buy_asset.id < sell_asset.id )
   {
      symbol_a = buy_asset.symbol;
      symbol_b = sell_asset.symbol;
   }
   else
   {
      symbol_b = buy_asset.symbol;
      symbol_a = sell_asset.symbol;
   }

   const auto& pool_idx = _db.get_index< asset_liquidity_pool_index >().indices().get< by_asset_pair >();

   liquidity_pool_api_obj buy_core_pool;
   liquidity_pool_api_obj sell_core_pool;
   liquidity_pool_api_obj buy_usd_pool;
   liquidity_pool_api_obj sell_usd_pool;
   liquidity_pool_api_obj direct_pool;

   auto pool_itr = pool_idx.find( boost::make_tuple( symbol_a, symbol_b ) );
   if( pool_itr != pool_idx.end() )
   {
      direct_pool = liquidity_pool_api_obj( *pool_itr );
      results.push_back( direct_pool );
   }

   if( asset_symbol_type( buy_symbol ) != SYMBOL_COIN )
   {
      auto pool_itr = pool_idx.find( boost::make_tuple( SYMBOL_COIN, asset_symbol_type( buy_symbol ) ) );
      if( pool_itr != pool_idx.end() )
      {
         buy_core_pool = liquidity_pool_api_obj( *pool_itr );
         results.push_back( buy_core_pool );
      }
   }

   if( asset_symbol_type( sell_symbol ) != SYMBOL_COIN )
   {
      auto pool_itr = pool_idx.find( boost::make_tuple( SYMBOL_COIN, asset_symbol_type( sell_symbol ) ) );
      if( pool_itr != pool_idx.end() )
      {
         sell_core_pool = liquidity_pool_api_obj( *pool_itr );
         results.push_back( sell_core_pool );
      }
   }

   if( asset_symbol_type( buy_symbol ) != SYMBOL_USD )
   {
      auto pool_itr = pool_idx.find( boost::make_tuple( SYMBOL_USD, asset_symbol_type( buy_symbol ) ) );
      if( pool_itr != pool_idx.end() )
      {
         buy_usd_pool = liquidity_pool_api_obj( *pool_itr );
         results.push_back( buy_usd_pool );
      }
   }

   if( asset_symbol_type( sell_symbol ) != SYMBOL_USD )
   {
      auto pool_itr = pool_idx.find( boost::make_tuple( SYMBOL_USD, asset_symbol_type( sell_symbol ) ) );
      if( pool_itr != pool_idx.end() )
      {
         sell_usd_pool = liquidity_pool_api_obj( *pool_itr );
         results.push_back( sell_usd_pool );
      }
   }
   
   return results; 
}


vector< option_pool_api_obj > database_api::get_option_pools( string buy_symbol, string sell_symbol )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_option_pools( buy_symbol, sell_symbol );
   });
}

vector< option_pool_api_obj > database_api_impl::get_option_pools( string buy_symbol, string sell_symbol )const
{
   FC_ASSERT( buy_symbol != sell_symbol, 
      "Buy Symbol cannot be equal to be Sell symbol." );

   vector< option_pool_api_obj > results;

   const asset_object& buy_asset = _db.get_asset( asset_symbol_type( buy_symbol ) );
   const asset_object& sell_asset = _db.get_asset( asset_symbol_type( sell_symbol ) );
   asset_symbol_type symbol_a;
   asset_symbol_type symbol_b;

   if( buy_asset.id < sell_asset.id )
   {
      symbol_a = buy_asset.symbol;
      symbol_b = sell_asset.symbol;
   }
   else
   {
      symbol_b = buy_asset.symbol;
      symbol_a = sell_asset.symbol;
   }

   const auto& pool_idx = _db.get_index< asset_option_pool_index >().indices().get< by_asset_pair >();

   option_pool_api_obj buy_core_pool;
   option_pool_api_obj sell_core_pool;
   option_pool_api_obj buy_usd_pool;
   option_pool_api_obj sell_usd_pool;
   option_pool_api_obj direct_pool;

   auto pool_itr = pool_idx.find( boost::make_tuple( symbol_a, symbol_b ) );
   if( pool_itr != pool_idx.end() )
   {
      direct_pool = option_pool_api_obj( *pool_itr );
      results.push_back( direct_pool );
   }

   if( asset_symbol_type( buy_symbol ) != SYMBOL_COIN )
   {
      auto pool_itr = pool_idx.find( boost::make_tuple( SYMBOL_COIN, asset_symbol_type( buy_symbol ) ) );
      if( pool_itr != pool_idx.end() )
      {
         buy_core_pool = option_pool_api_obj( *pool_itr );
         results.push_back( buy_core_pool );
      }
   }

   if( asset_symbol_type( sell_symbol ) != SYMBOL_COIN )
   {
      auto pool_itr = pool_idx.find( boost::make_tuple( SYMBOL_COIN, asset_symbol_type( sell_symbol ) ) );
      if( pool_itr != pool_idx.end() )
      {
         sell_core_pool = option_pool_api_obj( *pool_itr );
         results.push_back( sell_core_pool );
      }
   }

   if( asset_symbol_type( buy_symbol ) != SYMBOL_USD )
   {
      auto pool_itr = pool_idx.find( boost::make_tuple( SYMBOL_USD, asset_symbol_type( buy_symbol ) ) );
      if( pool_itr != pool_idx.end() )
      {
         buy_usd_pool = option_pool_api_obj( *pool_itr );
         results.push_back( buy_usd_pool );
      }
   }

   if( asset_symbol_type( sell_symbol ) != SYMBOL_USD )
   {
      auto pool_itr = pool_idx.find( boost::make_tuple( SYMBOL_USD, asset_symbol_type( sell_symbol ) ) );
      if( pool_itr != pool_idx.end() )
      {
         sell_usd_pool = option_pool_api_obj( *pool_itr );
         results.push_back( sell_usd_pool );
      }
   }
   
   return results;
}

market_state database_api::get_market_state( string buy_symbol, string sell_symbol )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_market_state( buy_symbol, sell_symbol );
   });
}

market_state database_api_impl::get_market_state( string buy_symbol, string sell_symbol )const
{
   market_state results;

   results.limit_orders = get_limit_orders( buy_symbol, sell_symbol );
   results.margin_orders = get_margin_orders( buy_symbol, sell_symbol );
   results.option_orders = get_option_orders( buy_symbol, sell_symbol );

   const asset_object& buy_asset = _db.get_asset( buy_symbol );
   const asset_object& sell_asset = _db.get_asset( sell_symbol );

   if( buy_asset.is_market_issued() )
   {
      const asset_stablecoin_data_object& buy_stablecoin = _db.get_stablecoin_data( buy_symbol );
      if( buy_stablecoin.backing_asset == sell_symbol )
      {
         results.call_orders = get_call_orders( buy_symbol, sell_symbol );
      }
   }
   if( sell_asset.is_market_issued() )
   {
      const asset_stablecoin_data_object& sell_stablecoin = _db.get_stablecoin_data( sell_symbol );
      if( sell_stablecoin.backing_asset == buy_symbol )
      {
         results.call_orders = get_call_orders( buy_symbol, sell_symbol );
      }
   }

   results.auction_orders = get_auction_orders( buy_symbol, sell_symbol );
   results.liquidity_pools = get_liquidity_pools( buy_symbol, sell_symbol );
   results.option_pools = get_option_pools( buy_symbol, sell_symbol );

   vector< string > assets;

   assets.push_back( buy_symbol );
   assets.push_back( sell_symbol );
   results.credit_pools = get_credit_pools( assets );
   results.credit_loans = get_credit_loans( buy_symbol, sell_symbol );

   return results; 
}


   //=============//
   // === Ads === //
   //=============//


vector< account_ad_state > database_api::get_account_ads( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_account_ads( names );
   });
}

vector< account_ad_state > database_api_impl::get_account_ads( vector< string > names )const
{
   vector< account_ad_state > results;
   results.reserve( names.size() );

   const auto& creative_idx   = _db.get_index< ad_creative_index >().indices().get< by_latest >();
   const auto& campaign_idx   = _db.get_index< ad_campaign_index >().indices().get< by_latest >();
   const auto& audience_idx   = _db.get_index< ad_audience_index >().indices().get< by_latest >();
   const auto& inventory_idx  = _db.get_index< ad_inventory_index >().indices().get< by_latest >();

   const auto& creative_id_idx   = _db.get_index< ad_creative_index >().indices().get< by_creative_id >();
   const auto& campaign_id_idx   = _db.get_index< ad_campaign_index >().indices().get< by_campaign_id >();
   const auto& audience_id_idx   = _db.get_index< ad_audience_index >().indices().get< by_audience_id >();
   const auto& inventory_id_idx  = _db.get_index< ad_inventory_index >().indices().get< by_inventory_id >();

   const auto& bidder_idx     = _db.get_index< ad_bid_index >().indices().get< by_bidder_updated >();
   const auto& account_idx    = _db.get_index< ad_bid_index >().indices().get< by_account_updated >();
   const auto& author_idx     = _db.get_index< ad_bid_index >().indices().get< by_author_updated >();
   const auto& provider_idx   = _db.get_index< ad_bid_index >().indices().get< by_provider_updated >();

   for( auto name : names )
   {
      account_ad_state astate;

      auto creative_itr = creative_idx.lower_bound( name );
      while( creative_itr != creative_idx.end() && creative_itr->author == name )
      {
         astate.creatives.push_back( ad_creative_api_obj( *creative_itr ) );
         ++creative_itr;
      }

      auto campaign_itr = campaign_idx.lower_bound( name );
      while( campaign_itr != campaign_idx.end() && campaign_itr->account == name )
      {
         astate.campaigns.push_back( ad_campaign_api_obj( *campaign_itr ) );
         ++campaign_itr;
      }

      auto audience_itr = audience_idx.lower_bound( name );
      while( audience_itr != audience_idx.end() && audience_itr->account == name )
      {
         astate.audiences.push_back( ad_audience_api_obj( *audience_itr ) );
         ++audience_itr;
      }

      auto inventory_itr = inventory_idx.lower_bound( name );
      while( inventory_itr != inventory_idx.end() && inventory_itr->provider == name )
      {
         astate.inventories.push_back( ad_inventory_api_obj( *inventory_itr ) );
         ++inventory_itr;
      }

      auto bidder_itr = bidder_idx.lower_bound( name );
      while( bidder_itr != bidder_idx.end() && bidder_itr->bidder == name )
      {
         astate.created_bids.push_back( ad_bid_api_obj( *bidder_itr ) );
         ++bidder_itr;
      }

      auto account_itr = account_idx.lower_bound( name );
      while( account_itr != account_idx.end() && account_itr->account == name )
      {
         astate.account_bids.push_back( ad_bid_api_obj( *account_itr ) );
         ++account_itr;
      }

      auto author_itr = author_idx.lower_bound( name );
      while( author_itr != author_idx.end() && author_itr->author == name )
      {
         astate.creative_bids.push_back( ad_bid_api_obj( *author_itr ) );
         ++author_itr;
      }

      auto provider_itr = provider_idx.lower_bound( name );
      while( provider_itr != provider_idx.end() && provider_itr->provider == name )
      {
         astate.incoming_bids.push_back( ad_bid_state( *provider_itr ) );

         auto cr_itr = creative_id_idx.find( boost::make_tuple( provider_itr->author, provider_itr->creative_id ) );
         if( cr_itr != creative_id_idx.end() )
         {
            astate.incoming_bids.back().creative = ad_creative_api_obj( *cr_itr );
         }

         auto c_itr = campaign_id_idx.find( boost::make_tuple( provider_itr->account, provider_itr->campaign_id ) );
         if( c_itr != campaign_id_idx.end() )
         {
            astate.incoming_bids.back().campaign = ad_campaign_api_obj( *c_itr );
         }

         auto i_itr = inventory_id_idx.find( boost::make_tuple( provider_itr->provider, provider_itr->inventory_id ) );
         if( i_itr != inventory_id_idx.end() )
         {
            astate.incoming_bids.back().inventory = ad_inventory_api_obj( *i_itr );
         }

         auto a_itr = audience_id_idx.find( boost::make_tuple( provider_itr->bidder, provider_itr->audience_id ) );
         if( a_itr != audience_id_idx.end() )
         {
            astate.incoming_bids.back().audience = ad_audience_api_obj( *a_itr );
         }

         ++provider_itr;
      }

      results.push_back( astate );
   }

   return results;
}


vector< ad_bid_state > database_api::get_interface_audience_bids( const ad_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_interface_audience_bids( query );
   });
}

/**
 * Retrieves all bids for an interface that includes a specified
 * account in its audience set. 
 */
vector< ad_bid_state > database_api_impl::get_interface_audience_bids( const ad_query& query )const
{
   vector< ad_bid_state > results;
   account_name_type interface = query.interface;
   account_name_type viewer = query.viewer;

   ad_format_type format = ad_format_type::STANDARD_FORMAT;
   ad_metric_type metric = ad_metric_type::VIEW_METRIC;

   for( size_t i = 0; i < ad_format_values.size(); i++ )
   {
      if( query.ad_format == ad_format_values[ i ] )
      {
         format = ad_format_type( i );
         break;
      }
   }

   for( size_t i = 0; i < ad_metric_values.size(); i++ )
   {
      if( query.ad_metric == ad_metric_values[ i ] )
      {
         metric = ad_metric_type( i );
         break;
      }
   }
   
   const auto& creative_id_idx = _db.get_index< ad_creative_index >().indices().get< by_creative_id >();
   const auto& campaign_id_idx = _db.get_index< ad_campaign_index >().indices().get< by_campaign_id >();
   const auto& audience_id_idx = _db.get_index< ad_audience_index >().indices().get< by_audience_id >();
   const auto& inventory_id_idx = _db.get_index< ad_inventory_index >().indices().get< by_inventory_id >();
   const auto& provider_idx = _db.get_index< ad_bid_index >().indices().get< by_provider_metric_format_price >();

   auto provider_itr = provider_idx.lower_bound( boost::make_tuple( interface, metric, format ) );
   auto provider_end = provider_idx.upper_bound( boost::make_tuple( interface, metric, format ) );

   while( provider_itr != provider_idx.end() && 
      provider_itr != provider_end &&
      results.size() < query.limit )
   {
      auto a_itr = audience_id_idx.find( boost::make_tuple( provider_itr->bidder, provider_itr->audience_id ) );
      if( a_itr != audience_id_idx.end() )
      {
         const ad_audience_object& aud = *a_itr;
         if( aud.is_audience( viewer ) )
         {
            results.push_back( ad_bid_state( *provider_itr ) );
            results.back().audience = ad_audience_api_obj( *a_itr );
            auto cr_itr = creative_id_idx.find( boost::make_tuple( provider_itr->author, provider_itr->creative_id ) );
            if( cr_itr != creative_id_idx.end() )
            {
               results.back().creative = ad_creative_api_obj( *cr_itr );
            }
            auto c_itr = campaign_id_idx.find( boost::make_tuple( provider_itr->account, provider_itr->campaign_id ) );
            if( c_itr != campaign_id_idx.end() )
            {
               results.back().campaign = ad_campaign_api_obj( *c_itr );
            }
            auto i_itr = inventory_id_idx.find( boost::make_tuple( provider_itr->provider, provider_itr->inventory_id ) );
            if( i_itr != inventory_id_idx.end() )
            {
               results.back().inventory = ad_inventory_api_obj( *i_itr );
            }
         }
      }
      ++provider_itr;
   }
   return results;
}


   //==================//
   // === Products === //
   //==================//


product_sale_api_obj database_api::get_product_sale( string seller, string product_id )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_product_sale( seller, product_id );
   });
}


/**
 * Retrieves a list of products and their purchase orders by ID.
 */
product_sale_api_obj database_api_impl::get_product_sale( string seller, string product_id )const
{
   product_sale_api_obj results;
   
   const auto& product_idx = _db.get_index< product_sale_index >().indices().get< by_product_id >();
   
   auto product_itr = product_idx.find( boost::make_tuple( seller, product_id ) );
   if( product_itr != product_idx.end() )
   {
      results = product_sale_api_obj( *product_itr );
   }

   return results;
}


product_auction_sale_api_obj database_api::get_product_auction_sale( string seller, string auction_id )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_product_auction_sale( seller, auction_id );
   });
}


product_auction_sale_api_obj database_api_impl::get_product_auction_sale( string seller, string auction_id )const
{
   product_auction_sale_api_obj results;
   
   const auto& product_idx = _db.get_index< product_auction_sale_index >().indices().get< by_auction_id >();
   
   auto product_itr = product_idx.find( boost::make_tuple( seller, auction_id ) );
   if( product_itr != product_idx.end() )
   {
      results = product_auction_sale_api_obj( *product_itr );
   }

   return results;
}


vector< account_product_state > database_api::get_account_products( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_account_products( names );
   });
}


/**
 * Retrieves a list of products and their purchase orders according to the sellers.
 */
vector< account_product_state > database_api_impl::get_account_products( vector< string > names )const
{
   vector< account_product_state > results;
   
   const auto& product_idx = _db.get_index< product_sale_index >().indices().get< by_product_id >();
   const auto& seller_purchase_idx = _db.get_index< product_purchase_index >().indices().get< by_product_id >();
   const auto& buyer_purchase_idx = _db.get_index< product_purchase_index >().indices().get< by_order_id >();

   const auto& auction_idx = _db.get_index< product_auction_sale_index >().indices().get< by_auction_id >();
   const auto& seller_bid_idx = _db.get_index< product_auction_bid_index >().indices().get< by_auction_id >();
   const auto& buyer_bid_idx = _db.get_index< product_auction_bid_index >().indices().get< by_bid_id >();
   
   auto product_itr = product_idx.begin();
   auto buyer_purchase_itr = buyer_purchase_idx.begin();
   auto seller_purchase_itr = seller_purchase_idx.begin();

   auto auction_itr = auction_idx.begin();
   auto seller_bid_itr = seller_bid_idx.begin();
   auto buyer_bid_itr = buyer_bid_idx.begin();

   for( auto acc : names )
   {
      account_product_state pstate;
      product_itr = product_idx.lower_bound( acc );

      while( product_itr != product_idx.end() && 
         product_itr->account == acc )
      {
         pstate.seller_products.push_back( product_sale_api_obj( *product_itr ) );
         ++product_itr;
      }

      seller_purchase_itr = seller_purchase_idx.lower_bound( acc );

      while( seller_purchase_itr != seller_purchase_idx.end() && 
         seller_purchase_itr->seller == acc )
      {
         pstate.seller_orders.push_back( product_purchase_api_obj( *seller_purchase_itr ) );
         ++seller_purchase_itr;
      }

      buyer_purchase_itr = buyer_purchase_idx.lower_bound( acc );

      while( buyer_purchase_itr != buyer_purchase_idx.end() && 
         buyer_purchase_itr->buyer == acc )
      {
         pstate.buyer_orders.push_back( product_purchase_api_obj( *buyer_purchase_itr ) );
         ++buyer_purchase_itr;
      }

      flat_set< pair < account_name_type, string > > buyer_products;

      for( auto product : pstate.buyer_orders )
      {
         buyer_products.insert( std::make_pair( product.seller, product.product_id ) );
      }

      for( auto product : buyer_products )
      {
         product_itr = product_idx.find( boost::make_tuple( product.first, product.second ) );
         if( product_itr != product_idx.end() )
         {
            pstate.buyer_products.push_back( product_sale_api_obj( *product_itr ) );
         }
      }

      auction_itr = auction_idx.lower_bound( acc );

      while( auction_itr != auction_idx.end() && 
         auction_itr->account == acc )
      {
         pstate.seller_auctions.push_back( product_auction_sale_api_obj( *auction_itr ) );
         ++auction_itr;
      }

      seller_bid_itr = seller_bid_idx.lower_bound( acc );

      while( seller_bid_itr != seller_bid_idx.end() && 
         seller_bid_itr->seller == acc )
      {
         pstate.seller_bids.push_back( product_auction_bid_api_obj( *seller_bid_itr ) );
         ++seller_bid_itr;
      }

      buyer_bid_itr = buyer_bid_idx.lower_bound( acc );

      while( buyer_bid_itr != buyer_bid_idx.end() && 
         buyer_bid_itr->buyer == acc )
      {
         pstate.buyer_bids.push_back( product_auction_bid_api_obj( *buyer_bid_itr ) );
         ++buyer_bid_itr;
      }

      flat_set< pair < account_name_type, string > > buyer_auctions;

      for( auto bid : pstate.buyer_bids )
      {
         buyer_auctions.insert( std::make_pair( bid.seller, bid.bid_id ) );
      }

      for( auto auction : buyer_auctions )
      {
         auction_itr = auction_idx.find( boost::make_tuple( auction.first, auction.second ) );
         if( auction_itr != auction_idx.end() )
         {
            pstate.buyer_auctions.push_back( product_auction_sale_api_obj( *auction_itr ) );
         }
      }

      results.push_back( pstate );
   }
   
   return results;
}


   //====================//
   // === Graph Data === //
   //====================//


graph_data_state database_api::get_graph_query( const graph_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_graph_query( query );
   });
}


/**
 * Retreives a series of graph nodes and edges based on the query.
 */
graph_data_state database_api_impl::get_graph_query( const graph_query& query )const
{
   graph_data_state results;

   const auto& node_idx = _db.get_index< graph_node_index >().indices().get< by_account_id >();
   const auto& edge_idx = _db.get_index< graph_edge_index >().indices().get< by_account_id >();

   auto node_itr = node_idx.begin();
   auto edge_itr = edge_idx.begin();
   
   vector< graph_node_api_obj > nodes;
   vector< graph_edge_api_obj > edges;
   
   nodes.reserve( query.limit );
   edges.reserve( query.limit );

   bool found = false;
   bool not_found = false;

   while( node_itr != node_idx.end() )
   {
      if( !query.include_private && node_itr->is_encrypted() )
      {
         ++node_itr;
         continue;
      }

      if( query.select_accounts.find( node_itr->account ) == query.select_accounts.end() )
      {
         ++node_itr;
         continue;
      }

      if( query.filter_accounts.find( node_itr->account ) != query.filter_accounts.end() )
      {
         ++node_itr;
         continue;
      }

      // Must contain all intersect select node types
      for( auto node_type : node_itr->node_types )
      {
         not_found = false;

         if( query.intersect_select_node_types.find( node_type ) == query.intersect_select_node_types.end() )
         {
            not_found = true;
            break;
         }
      }

      if( not_found )
      {
         ++node_itr;
         continue;
      }

      // Must not contain all intersect filter node types
      for( auto node_type : node_itr->node_types )
      {
         not_found = false;

         if( query.intersect_filter_node_types.find( node_type ) == query.intersect_filter_node_types.end() )
         {
            not_found = true;
            break;
         }
      }

      if( !not_found )
      {
         ++node_itr;
         continue;
      }

      // Must contain any union node types
      for( auto node_type : node_itr->node_types )
      {
         found = false;

         if( query.union_select_node_types.find( node_type ) != query.union_select_node_types.end() )
         {
            found = true;
            break;
         }
      }

      if( !found )
      {
         ++node_itr;
         continue;
      }

      // Must not contain any union node types
      for( auto node_type : node_itr->node_types )
      {
         found = false;

         if( query.union_filter_node_types.find( node_type ) != query.union_filter_node_types.end() )
         {
            found = true;
            break;
         }
      }

      if( found )
      {
         ++node_itr;
         continue;
      }

      flat_map< fixed_string_32, fixed_string_32 > attribute_map = node_itr->attribute_map();

      // Must match all intersect select attribute values
      for( size_t i = 0; i < query.node_intersect_select_attributes.size(); i++ )
      {
         not_found = false;

         if( attribute_map[ query.node_intersect_select_attributes[ i ] ] != query.node_intersect_select_values[ i ] )
         {
            not_found = true;
            break;
         }
      }

      if( not_found )
      {
         ++node_itr;
         continue;
      }

      // Must not match all intersect select attribute values
      for( size_t i = 0; i < query.node_intersect_filter_attributes.size(); i++ )
      {
         not_found = false;

         if( attribute_map[ query.node_intersect_filter_attributes[ i ] ] != query.node_intersect_filter_values[ i ] )
         {
            not_found = true;
            break;
         }
      }

      if( !not_found )
      {
         ++node_itr;
         continue;
      }

      // Must contain any union select attribute values
      for( size_t i = 0; i < query.node_union_select_attributes.size(); i++ )
      {
         found = false;

         if( attribute_map[ query.node_union_select_attributes[ i ] ] == query.node_union_select_values[ i ] )
         {
            found = true;
            break;
         }
      }

      if( !found )
      {
         ++node_itr;
         continue;
      }

      // Must not contain any union filter attribute values
      for( size_t i = 0; i < query.node_union_filter_attributes.size(); i++ )
      {
         found = false;

         if( attribute_map[ query.node_union_filter_attributes[ i ] ] == query.node_union_filter_values[ i ] )
         {
            found = true;
            break;
         }
      }

      if( found )
      {
         ++node_itr;
         continue;
      }

      nodes.push_back( *node_itr );
      ++node_itr;
   }

   results.nodes = nodes;

   while( edge_itr != edge_idx.end() )
   {
      if( !query.include_private && edge_itr->is_encrypted() )
      {
         ++edge_itr;
         continue;
      }

      if( query.select_accounts.find( edge_itr->account ) == query.select_accounts.end() )
      {
         ++edge_itr;
         continue;
      }

      if( query.filter_accounts.find( edge_itr->account ) != query.filter_accounts.end() )
      {
         ++edge_itr;
         continue;
      }

      // Must contain all intersect select edge types
      for( auto edge_type : edge_itr->edge_types )
      {
         not_found = false;

         if( query.intersect_select_edge_types.find( edge_type ) == query.intersect_select_edge_types.end() )
         {
            not_found = true;
            break;
         }
      }

      if( not_found )
      {
         ++edge_itr;
         continue;
      }

      // Must not contain all intersect filter edge types
      for( auto edge_type : edge_itr->edge_types )
      {
         not_found = false;

         if( query.intersect_filter_edge_types.find( edge_type ) == query.intersect_filter_edge_types.end() )
         {
            not_found = true;
            break;
         }
      }

      if( !not_found )
      {
         ++edge_itr;
         continue;
      }

      // Must contain any union edge types
      for( auto edge_type : edge_itr->edge_types )
      {
         found = false;

         if( query.union_select_edge_types.find( edge_type ) != query.union_select_edge_types.end() )
         {
            found = true;
            break;
         }
      }

      if( !found )
      {
         ++edge_itr;
         continue;
      }

      // Must not contain any union edge types
      for( auto edge_type : edge_itr->edge_types )
      {
         found = false;

         if( query.union_filter_edge_types.find( edge_type ) != query.union_filter_edge_types.end() )
         {
            found = true;
            break;
         }
      }

      if( found )
      {
         ++edge_itr;
         continue;
      }

      flat_map< fixed_string_32, fixed_string_32 > attribute_map = edge_itr->attribute_map();

      // Must match all intersect select attribute values
      for( size_t i = 0; i < query.edge_intersect_select_attributes.size(); i++ )
      {
         not_found = false;

         if( attribute_map[ query.edge_intersect_select_attributes[ i ] ] != query.edge_intersect_select_values[ i ] )
         {
            not_found = true;
            break;
         }
      }

      if( not_found )
      {
         ++edge_itr;
         continue;
      }

      // Must not match all intersect select attribute values
      for( size_t i = 0; i < query.edge_intersect_filter_attributes.size(); i++ )
      {
         not_found = false;

         if( attribute_map[ query.edge_intersect_filter_attributes[ i ] ] != query.edge_intersect_filter_values[ i ] )
         {
            not_found = true;
            break;
         }
      }

      if( !not_found )
      {
         ++edge_itr;
         continue;
      }

      // Must contain any union select attribute values
      for( size_t i = 0; i < query.edge_union_select_attributes.size(); i++ )
      {
         found = false;

         if( attribute_map[ query.edge_union_select_attributes[ i ] ] == query.edge_union_select_values[ i ] )
         {
            found = true;
            break;
         }
      }

      if( !found )
      {
         ++edge_itr;
         continue;
      }

      // Must not contain any union filter attribute values
      for( size_t i = 0; i < query.edge_union_filter_attributes.size(); i++ )
      {
         found = false;

         if( attribute_map[ query.edge_union_filter_attributes[ i ] ] == query.edge_union_filter_values[ i ] )
         {
            found = true;
            break;
         }
      }

      if( found )
      {
         ++edge_itr;
         continue;
      }

      edges.push_back( *edge_itr );
      ++edge_itr;
   }

   results.edges = edges;
   
   return results;
}


vector< graph_node_property_api_obj > database_api::get_graph_node_properties( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_graph_node_properties( names );
   });
}


vector< graph_node_property_api_obj > database_api_impl::get_graph_node_properties( vector< string > names )const
{
   vector< graph_node_property_api_obj > results;

   const auto& node_idx = _db.get_index< graph_node_property_index >().indices().get< by_node_type >();

   for( auto node_type : names )
   {
      auto node_itr = node_idx.find( node_type );
      if( node_itr != node_idx.end() )
      {
         results.push_back( *node_itr );
      }
   }

   return results;
}


vector< graph_edge_property_api_obj > database_api::get_graph_edge_properties( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_graph_edge_properties( names );
   });
}


vector< graph_edge_property_api_obj > database_api_impl::get_graph_edge_properties( vector< string > names )const
{
   vector< graph_edge_property_api_obj > results;

   const auto& edge_idx = _db.get_index< graph_edge_property_index >().indices().get< by_edge_type >();

   for( auto edge_type : names )
   {
      auto edge_itr = edge_idx.find( edge_type );
      if( edge_itr != edge_idx.end() )
      {
         results.push_back( *edge_itr );
      }
   }

   return results;
}


   //================//
   // === Search === //
   //================//


search_result_state database_api::get_search_query( const search_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_search_query( query );
   });
}

/**
 * Retreives a series of accounts, communities, tags, assets and posts according to the 
 * lowest edit distance between the search query and the names of the objects.
 */
search_result_state database_api_impl::get_search_query( const search_query& query )const
{
   search_result_state results;
   string q = query.query;
   size_t margin = ( ( q.size() * query.margin_percent ) / PERCENT_100 ) + 1;

   const auto& account_idx = _db.get_index< account_index >().indices().get< by_name >();
   const auto& community_idx = _db.get_index< community_index >().indices().get< by_name >();
   const auto& tag_idx = _db.get_index< account_tag_following_index >().indices().get< by_tag >();
   const auto& asset_idx = _db.get_index< asset_index >().indices().get< by_symbol >();
   const auto& post_idx = _db.get_index< comment_index >().indices().get< by_title >();

   auto account_itr = account_idx.begin();
   auto community_itr = community_idx.begin();
   auto tag_itr = tag_idx.begin();
   auto asset_itr = asset_idx.begin();
   auto post_itr = post_idx.upper_bound( string() );    // Skip index posts with no title

   vector< pair< account_name_type, size_t > > accounts;
   vector< pair< community_name_type, size_t > > communities;
   vector< pair< tag_name_type, size_t > > tags;
   vector< pair< asset_symbol_type, size_t > > assets;
   vector< pair< string, size_t > > posts;

   accounts.reserve( account_idx.size() );
   communities.reserve( community_idx.size() );
   tags.reserve( tag_idx.size() );
   assets.reserve( asset_idx.size() );
   posts.reserve( post_idx.size() );

   results.accounts.reserve( query.limit );
   results.communities.reserve( query.limit );
   results.tags.reserve( query.limit );
   results.assets.reserve( query.limit );
   results.posts.reserve( query.limit );

   // Finds items that are within the specified margin of error by edit distance from the search term.
   // Sort items in Ascending order, lowest edit distance first.

   if( query.include_accounts )
   {
      while( account_itr != account_idx.end() )
      {
         size_t edit_dist = protocol::edit_distance( string( account_itr->name ), q );
         if( edit_dist <= margin )
         {
            accounts.push_back( std::make_pair( account_itr->name, edit_dist  ) );
         }
         ++account_itr;
      }

      std::sort( accounts.begin(), accounts.end(), [&]( pair< account_name_type, size_t > a, pair< account_name_type, size_t > b )
      {
         return a.second > b.second;
      });

      for( auto item : accounts )
      {
         if( results.accounts.size() < query.limit )
         {
            account_itr = account_idx.find( item.first );
            results.accounts.push_back( account_api_obj( *account_itr, _db ) );
         }
         else
         {
            break;
         }
      }
   }

   if( query.include_communities )
   {
      while( community_itr != community_idx.end() )
      {
         size_t edit_dist = protocol::edit_distance( string( community_itr->name ), q );
         if( edit_dist <= margin )
         {
            communities.push_back( std::make_pair( community_itr->name, edit_dist ) );
         }
         ++community_itr;
      }

      std::sort( communities.begin(), communities.end(), [&]( pair< community_name_type, size_t > a, pair< community_name_type, size_t > b )
      {
         return a.second > b.second;
      });

      for( auto item : communities )
      {
         if( results.communities.size() < query.limit )
         {
            community_itr = community_idx.find( item.first );
            results.communities.push_back( community_api_obj( *community_itr ) );
         }
         else
         {
            break;
         }
      }
   }

   if( query.include_tags )
   {
      while( tag_itr != tag_idx.end() )
      {
         size_t edit_dist = protocol::edit_distance( string( tag_itr->tag ), q );
         if( edit_dist <= margin )
         {
            tags.push_back( std::make_pair( tag_itr->tag, edit_dist ) );
         }
         ++tag_itr;
      }

      std::sort( tags.begin(), tags.end(), [&]( pair< tag_name_type, size_t > a, pair< tag_name_type, size_t > b )
      {
         return a.second > b.second;
      });

      for( auto item : tags )
      {
         if( results.tags.size() < query.limit )
         {
            tag_itr = tag_idx.find( item.first );
            results.tags.push_back( account_tag_following_api_obj( *tag_itr ) );
         }
         else
         {
            break;
         }
      }
   }

   if( query.include_assets )
   {
      while( asset_itr != asset_idx.end() )
      {
         size_t edit_dist = protocol::edit_distance( string( asset_itr->symbol ), q );
         if( edit_dist <= margin )
         {
            assets.push_back( std::make_pair( asset_itr->symbol, edit_dist ) );
         }
         ++asset_itr;
      }

      std::sort( assets.begin(), assets.end(), [&]( pair< asset_symbol_type, size_t > a, pair< asset_symbol_type, size_t > b )
      {
         return a.second > b.second;
      });

      for( auto item : assets )
      {
         if( results.assets.size() < query.limit )
         {
            asset_itr = asset_idx.find( item.first );
            results.assets.push_back( asset_api_obj( *asset_itr ) );
         }
         else
         {
            break;
         }
      }
   }

   if( query.include_posts )
   {
      while( post_itr != post_idx.end() )
      {
         string title = to_string( post_itr->title );
         size_t edit_dist = protocol::edit_distance( title, q );
         if( edit_dist <= margin )
         {
            posts.push_back( std::make_pair( title, edit_dist ) );
         }
         ++post_itr;
      }

      std::sort( posts.begin(), posts.end(), [&]( pair< string, size_t > a, pair< string, size_t > b )
      {
         return a.second > b.second;
      });

      for( auto item : posts )
      {
         if( results.posts.size() < query.limit )
         {
            post_itr = post_idx.find( item.first );
            results.posts.push_back( discussion( *post_itr ) );
         }
         else
         {
            break;
         }
      }
   }

   return results;
}


   //=================================//
   // === Blocks and Transactions === //
   //=================================//


optional< block_header > database_api::get_block_header( uint64_t block_num )const
{
   FC_ASSERT( !my->_disable_get_block,
      "get_block_header is disabled on this node." );

   return my->_db.with_read_lock( [&]()
   {
      return my->get_block_header( block_num );
   });
}


optional< block_header > database_api_impl::get_block_header( uint64_t block_num ) const
{
   auto results = _db.fetch_block_by_number( block_num );
   if( results )
   {
      return *results;
   }
      
   return {};
}


optional< signed_block_api_obj > database_api::get_block( uint64_t block_num )const
{
   FC_ASSERT( !my->_disable_get_block,
      "get_block is disabled on this node." );

   return my->_db.with_read_lock( [&]()
   {
      return my->get_block( block_num );
   });
}


optional< signed_block_api_obj > database_api_impl::get_block( uint64_t block_num )const
{
   return _db.fetch_block_by_number( block_num );
}


vector< applied_operation > database_api::get_ops_in_block( uint64_t block_num, bool only_virtual )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_ops_in_block( block_num, only_virtual );
   });
}


vector< applied_operation > database_api_impl::get_ops_in_block(uint64_t block_num, bool only_virtual)const
{
   const auto& operation_idx = _db.get_index< operation_index >().indices().get< by_location >();
   auto operation_itr = operation_idx.lower_bound( block_num );
   vector< applied_operation> results;
   applied_operation temp;

   while( operation_itr != operation_idx.end() && operation_itr->block == block_num )
   {
      temp = *operation_itr;
      if( !only_virtual || is_virtual_operation(temp.op) )
      {
         results.push_back(temp);
      } 
      ++operation_itr;
   }
   return results;
}


std::string database_api::get_transaction_hex( const signed_transaction& trx )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_transaction_hex( trx );
   });
}

std::string database_api_impl::get_transaction_hex( const signed_transaction& trx )const
{
   return fc::to_hex( fc::raw::pack( trx ) );
}

annotated_signed_transaction database_api::get_transaction( transaction_id_type id )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_transaction( id );
   });
}

annotated_signed_transaction database_api_impl::get_transaction( transaction_id_type id )const
{
   #ifndef SKIP_BY_TX_ID
   const auto& operation_idx = _db.get_index< operation_index >().indices().get< by_transaction_id >();
   auto operation_itr = operation_idx.lower_bound( id );

   annotated_signed_transaction results;

   if( operation_itr != operation_idx.end() && operation_itr->trx_id == id )
   {
      auto blk = _db.fetch_block_by_number( operation_itr->block );
      FC_ASSERT( blk.valid() );
      FC_ASSERT( blk->transactions.size() > operation_itr->trx_in_block );
      results = blk->transactions[ operation_itr->trx_in_block ];
      results.block_num = operation_itr->block;
      results.transaction_num = operation_itr->trx_in_block;
      return results;
   }

   #endif

   FC_ASSERT( false, "Unknown Transaction ${t}", ("t",id));
}

set< public_key_type > database_api::get_required_signatures( const signed_transaction& trx, const flat_set< public_key_type >& available_keys )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_required_signatures( trx, available_keys );
   });
}

set< public_key_type > database_api_impl::get_required_signatures( const signed_transaction& trx, const flat_set < public_key_type >& available_keys )const
{
   set< public_key_type > results = trx.get_required_signatures(
      CHAIN_ID,
      available_keys,
      [&]( string account_name ){ return authority( _db.get< account_authority_object, by_account >( account_name ).active_auth  ); },
      [&]( string account_name ){ return authority( _db.get< account_authority_object, by_account >( account_name ).owner_auth   ); },
      [&]( string account_name ){ return authority( _db.get< account_authority_object, by_account >( account_name ).posting_auth ); },
      MAX_SIG_CHECK_DEPTH );
   return results;
}

set< public_key_type > database_api::get_potential_signatures( const signed_transaction& trx )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_potential_signatures( trx );
   });
}

set< public_key_type > database_api_impl::get_potential_signatures( const signed_transaction& trx )const
{
   set< public_key_type > results;
   trx.get_required_signatures(
      CHAIN_ID,
      flat_set< public_key_type >(),
      [&]( account_name_type account_name )
      {
         const auto& auth = _db.get< account_authority_object, by_account >(account_name).active_auth;
         for( const auto& k : auth.get_keys() )
            results.insert(k);
         return authority( auth );
      },
      [&]( account_name_type account_name )
      {
         const auto& auth = _db.get< account_authority_object, by_account >(account_name).owner_auth;
         for( const auto& k : auth.get_keys() )
            results.insert(k);
         return authority( auth );
      },
      [&]( account_name_type account_name )
      {
         const auto& auth = _db.get< account_authority_object, by_account >(account_name).posting_auth;
         for( const auto& k : auth.get_keys() )
            results.insert(k);
         return authority( auth );
      },
      MAX_SIG_CHECK_DEPTH
   );

   return results;
}

bool database_api::verify_authority( const signed_transaction& trx ) const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->verify_authority( trx );
   });
}

bool database_api_impl::verify_authority( const signed_transaction& trx )const
{
   trx.verify_authority( 
      CHAIN_ID,
      [&]( string account_name ){ return authority( _db.get< account_authority_object, by_account >( account_name ).active_auth  ); },
      [&]( string account_name ){ return authority( _db.get< account_authority_object, by_account >( account_name ).owner_auth   ); },
      [&]( string account_name ){ return authority( _db.get< account_authority_object, by_account >( account_name ).posting_auth ); },
      MAX_SIG_CHECK_DEPTH );
   return true;
}

bool database_api::verify_account_authority( const string& name_or_id, const flat_set< public_key_type >& signers )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->verify_account_authority( name_or_id, signers );
   });
}

bool database_api_impl::verify_account_authority( const string& name, const flat_set< public_key_type >& keys )const
{
   FC_ASSERT( name.size() > 0,
      "Verify requets must include account name.");
   const account_object* account = _db.find< account_object, by_name >( name );
   FC_ASSERT( account != nullptr, 
      "No such account" );
   signed_transaction trx;
   transfer_operation op;
   op.from = account->name;
   op.signatory = account->name;
   trx.operations.emplace_back( op );

   return verify_authority( trx );    // Confirm authority is able to sign a transfer operation
}


   //======================//
   // === Posts + Tags === //
   //======================//



comment_interaction_state database_api::get_comment_interactions( string author, string permlink )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_comment_interactions( author, permlink );
   });
}

comment_interaction_state database_api_impl::get_comment_interactions( string author, string permlink )const
{
   comment_interaction_state results;

   const comment_object& comment = _db.get_comment( author, permlink );

   const auto& vote_idx = _db.get_index< comment_vote_index >().indices().get< by_comment_voter >();
   const auto& view_idx = _db.get_index< comment_view_index >().indices().get< by_comment_viewer >();
   const auto& share_idx = _db.get_index< comment_share_index >().indices().get< by_comment_sharer >();
   const auto& moderation_idx = _db.get_index< comment_moderation_index >().indices().get< by_comment_moderator >();

   comment_id_type cid(comment.id);

   auto vote_itr = vote_idx.lower_bound( cid );

   while( vote_itr != vote_idx.end() && 
      vote_itr->comment == cid )
   {
      vote_state vstate;

      vstate.voter = vote_itr->voter;
      vstate.weight = vote_itr->weight;
      vstate.reward = vote_itr->reward.value;
      vstate.percent = vote_itr->vote_percent;
      vstate.time = vote_itr->last_updated;
      results.votes.push_back( vstate );
      ++vote_itr;
   }

   auto view_itr = view_idx.lower_bound( cid );
   
   while( view_itr != view_idx.end() && 
      view_itr->comment == cid )
   {
      view_state vstate;

      vstate.viewer = view_itr->viewer;
      vstate.weight = view_itr->weight;
      vstate.reward = view_itr->reward.value;
      vstate.time = view_itr->created;
      results.views.push_back( vstate );
      ++view_itr;
   }
   
   auto share_itr = share_idx.lower_bound( cid );

   while( share_itr != share_idx.end() && 
      share_itr->comment == cid )
   {
      share_state sstate;

      sstate.sharer = share_itr->sharer;
      sstate.weight = share_itr->weight;
      sstate.reward = share_itr->reward.value;
      sstate.time = share_itr->created;

      results.shares.push_back( sstate );
      ++share_itr;
   }
  
   auto moderation_itr = moderation_idx.lower_bound( cid );

   while( moderation_itr != moderation_idx.end() && 
      moderation_itr->comment == cid )
   {
      moderation_state mstate;
      mstate.moderator = moderation_itr->moderator;
      for( auto tag : moderation_itr->tags )
      {
         mstate.tags.push_back( tag );
      }
      mstate.rating = moderation_itr->rating;
      mstate.details = to_string( moderation_itr->details );
      mstate.filter = moderation_itr->filter;
      mstate.time = moderation_itr->last_updated;

      results.moderation.push_back( mstate );
      ++moderation_itr;
   }

   return results;
}

vector< account_vote > database_api::get_account_votes( string account, string from_author, string from_permlink, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_account_votes( account, from_author, from_permlink, limit );
   });
}

vector< account_vote > database_api_impl::get_account_votes( string account, string from_author, string from_permlink, uint32_t limit )const
{
   vector< account_vote > results;
   limit = std::min( limit, uint32_t( 1000 ) );
   results.reserve( limit );

   const auto& com_vote_idx = _db.get_index< comment_vote_index >().indices().get< by_voter_comment >();

   auto com_vote_itr = com_vote_idx.lower_bound( account );
   auto com_vote_end = com_vote_idx.upper_bound( account );

   if( from_author.length() && from_permlink.length() )
   {
      const comment_object& com = _db.get_comment( from_author, from_permlink );
      auto from_itr = com_vote_idx.find( boost::make_tuple( account, com.id ) );
      if( from_itr != com_vote_idx.end() )
      {
         com_vote_itr = com_vote_idx.iterator_to( *from_itr );
      }
   }

   while( com_vote_itr != com_vote_end && results.size() < limit )
   {
      const comment_object& comment = _db.get( com_vote_itr->comment );
      account_vote avote;
      avote.author = comment.author;
      avote.permlink = to_string( comment.permlink );
      avote.weight = com_vote_itr->weight;
      avote.reward = com_vote_itr->reward.value;
      avote.percent = com_vote_itr->vote_percent;
      avote.time = com_vote_itr->last_updated;
      results.push_back( avote );
      ++com_vote_itr;
   }
   return results;
}

vector< account_view > database_api::get_account_views( string account, string from_author, string from_permlink, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_account_views( account, from_author, from_permlink, limit );
   });
}

vector< account_view > database_api_impl::get_account_views( string account, string from_author, string from_permlink, uint32_t limit )const
{
   vector< account_view > results;
   limit = std::min( limit, uint32_t( 1000 ) );
   results.reserve( limit );

   const auto& com_view_idx = _db.get_index< comment_view_index >().indices().get< by_viewer_comment >();
   auto com_view_itr = com_view_idx.lower_bound( account );
   auto com_view_end = com_view_idx.upper_bound( account );

   if( from_author.length() && from_permlink.length() )
   {
      const comment_object& com = _db.get_comment( from_author, from_permlink );
      auto from_itr = com_view_idx.find( boost::make_tuple( account, com.id ) );
      if( from_itr != com_view_idx.end() )
      {
         com_view_itr = com_view_idx.iterator_to( *from_itr );
      }
   }

   while( com_view_itr != com_view_end && results.size() < limit )
   {
      const comment_object& comment = _db.get( com_view_itr->comment );
      account_view aview;
      aview.author = comment.author;
      aview.permlink = to_string( comment.permlink );
      aview.weight = com_view_itr->weight;
      aview.reward = com_view_itr->reward.value;
      aview.time = com_view_itr->created;
      results.push_back( aview );
      ++com_view_itr;
   }
   return results;
}

vector< account_share > database_api::get_account_shares( string account, string from_author, string from_permlink, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_account_shares( account, from_author, from_permlink, limit );
   });
}

vector< account_share > database_api_impl::get_account_shares( string account, string from_author, string from_permlink, uint32_t limit )const
{
   vector< account_share > results;
   limit = std::min( limit, uint32_t( 1000 ) );
   results.reserve( limit );

   const auto& com_share_idx = _db.get_index< comment_share_index >().indices().get< by_sharer_comment >();
   auto com_share_itr = com_share_idx.lower_bound( account );
   auto com_share_end = com_share_idx.upper_bound( account );

   if( from_author.length() && from_permlink.length() )
   {
      const comment_object& com = _db.get_comment( from_author, from_permlink );
      auto from_itr = com_share_idx.find( boost::make_tuple( account, com.id ) );
      if( from_itr != com_share_idx.end() )
      {
         com_share_itr = com_share_idx.iterator_to( *from_itr );
      }
   }

   while( com_share_itr != com_share_end && results.size() < limit )
   {
      const comment_object& comment = _db.get( com_share_itr->comment );
      account_share ashare;
      ashare.author = comment.author;
      ashare.permlink = to_string( comment.permlink );
      ashare.weight = com_share_itr->weight;
      ashare.reward = com_share_itr->reward.value;
      ashare.time = com_share_itr->created;
      results.push_back( ashare );
      ++com_share_itr;
   }
   return results;
}

vector< account_moderation > database_api::get_account_moderation( string account, string from_author, string from_permlink, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_account_moderation( account, from_author, from_permlink, limit );
   });
}

vector< account_moderation > database_api_impl::get_account_moderation( string account, string from_author, string from_permlink, uint32_t limit )const
{
   vector< account_moderation > results;
   limit = std::min( limit, uint32_t( 1000 ) );
   results.reserve( limit );

   const auto& com_mod_idx = _db.get_index< comment_moderation_index >().indices().get< by_moderator_comment >();
   auto com_mod_itr = com_mod_idx.lower_bound( account );
   auto com_mod_end = com_mod_idx.upper_bound( account );

   if( from_author.length() && from_permlink.length() )
   {
      const comment_object& com = _db.get_comment( from_author, from_permlink );
      auto from_itr = com_mod_idx.find( boost::make_tuple( account, com.id ) );
      if( from_itr != com_mod_idx.end() )
      {
         com_mod_itr = com_mod_idx.iterator_to( *from_itr );
      }
   }

   while( com_mod_itr != com_mod_end && results.size() < limit )
   {
      const comment_object& comment = _db.get( com_mod_itr->comment );
      account_moderation amod;
      amod.author = comment.author;
      amod.permlink = to_string( comment.permlink );
      amod.tags.reserve( com_mod_itr->tags.size() );
      for( auto t : com_mod_itr->tags )
      {
         amod.tags.push_back( t );
      }
      amod.rating = com_mod_itr->rating;
      amod.details = to_string( com_mod_itr->details );
      amod.filter = com_mod_itr->filter;
      amod.time = com_mod_itr->last_updated;
      results.push_back( amod );
      ++com_mod_itr;
   }
   return results;
}

vector< account_tag_following_api_obj > database_api::get_account_tag_followings( vector< string > tags )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_account_tag_followings( tags );
   });
}

vector< account_tag_following_api_obj > database_api_impl::get_account_tag_followings( vector< string > tags )const
{
   vector< account_tag_following_api_obj > results;
   const auto& tag_idx = _db.get_index< account_tag_following_index >().indices().get< by_tag >();
   
   for( auto tag : tags )
   {
      auto tag_itr = tag_idx.find( tag );

      if( tag_itr != tag_idx.end() )
      {
         results.push_back( account_tag_following_api_obj( *tag_itr ) );
      }
   }
   return results;
}

vector< pair< tag_name_type, uint32_t > > database_api::get_tags_used_by_author( string author )const 
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_tags_used_by_author( author );
   });
}

vector< pair< tag_name_type, uint32_t > > database_api_impl::get_tags_used_by_author( string author )const 
{
   if( !_db.has_index<tags::author_tag_stats_index>() )
   {
      return vector< pair< tag_name_type, uint32_t > >();
   }

   const account_object* account_ptr = _db.find_account( author );
   FC_ASSERT( account_ptr != nullptr,
      "Account not found." );

   const auto& author_tag_idx = _db.get_index< tags::author_tag_stats_index >().indices().get< tags::by_author_posts_tag >();
   auto author_tag_itr = author_tag_idx.lower_bound( author );
   vector< pair< tag_name_type, uint32_t > > results;

   while( author_tag_itr != author_tag_idx.end() && 
      author_tag_itr->author == account_ptr->name && 
      results.size() < 1000 )
   {
      results.push_back( std::make_pair( author_tag_itr->tag, author_tag_itr->total_posts ) );
      ++author_tag_itr;
   }
   return results;
}

vector< tag_api_obj > database_api::get_top_tags( string after, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_top_tags( after, limit );
   });
}

vector< tag_api_obj > database_api_impl::get_top_tags( string after, uint32_t limit )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< tag_api_obj >();
   }
   
   vector< tag_api_obj > results;
   limit = std::min( limit, uint32_t( 1000 ) );
   results.reserve( limit );

   const auto& nidx = _db.get_index< tags::tag_stats_index >().indices().get< tags::by_tag >();
   const auto& ridx = _db.get_index< tags::tag_stats_index >().indices().get< tags::by_vote_power >();

   auto itr = ridx.begin();
   if( after != "" && nidx.size() )
   {
      auto nitr = nidx.lower_bound( after );
      if( nitr == nidx.end() )
      {
         itr = ridx.end();
      } 
      else
      {
         itr = ridx.iterator_to( *nitr );
      } 
   }

   while( itr != ridx.end() && results.size() < limit )
   {
      results.push_back( tag_api_obj( *itr ) );
      ++itr;
   }
   return results;
}


   //=====================//
   // === Discussions === //
   //=====================//


discussion database_api::get_content( string author, string permlink )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_content( author, permlink );
   });
}


discussion database_api_impl::get_content( string author, string permlink )const
{
   const auto& comment_idx = _db.get_index< comment_index >().indices().get< by_permlink >();
   auto itr = comment_idx.find( boost::make_tuple( author, permlink ) );
   if( itr != comment_idx.end() )
   {
      discussion results(*itr);

      comment_interaction_state cstate = get_comment_interactions( author, permlink );
      results.active_votes = cstate.votes;
      results.active_views = cstate.views;
      results.active_shares = cstate.shares;
      results.active_mod_tags = cstate.moderation;
      return results;
   }
   else
   {
      return discussion();
   }
}

vector< discussion > database_api::get_content_replies( string author, string permlink )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_content_replies( author, permlink );
   });
}

vector< discussion > database_api_impl::get_content_replies( string author, string permlink )const
{
   account_name_type acc_name = account_name_type( author );
   const auto& comment_idx = _db.get_index< comment_index >().indices().get< by_parent >();
   auto comment_itr = comment_idx.find( boost::make_tuple( acc_name, permlink ) );
   vector< discussion > results;

   while( comment_itr != comment_idx.end() && 
      comment_itr->parent_author == author && 
      to_string( comment_itr->parent_permlink ) == permlink )
   {
      results.push_back( discussion( *comment_itr ) );
      ++comment_itr;
   }
   return results;
}

vector< discussion > database_api::get_replies_by_last_update( account_name_type start_parent_author, string start_permlink, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_replies_by_last_update( start_parent_author, start_permlink, limit );
   });
}


vector< discussion > database_api_impl::get_replies_by_last_update( account_name_type start_parent_author, string start_permlink, uint32_t limit )const
{
   vector< discussion > results;

   limit = std::min( limit, uint32_t( 1000 ) );
   const auto& comment_idx = _db.get_index< comment_index >().indices().get< by_last_update >();
   auto comment_itr = comment_idx.begin();
   const account_name_type* parent_author = &start_parent_author;

   if( start_permlink.size() )
   {
      const comment_object& comment = _db.get_comment( start_parent_author, start_permlink );
      comment_itr = comment_idx.iterator_to( comment );
      parent_author = &comment.parent_author;
   }
   else if( start_parent_author.size() )
   {
      comment_itr = comment_idx.lower_bound( start_parent_author );
   }

   results.reserve( limit );

   while( comment_itr != comment_idx.end() && 
      results.size() < limit && 
      comment_itr->parent_author == *parent_author )
   {
      discussion d = get_discussion( comment_itr->id, false );
      results.push_back( d );
      ++comment_itr;
   }
   
   return results;
}

discussion database_api::get_discussion( comment_id_type id, uint32_t truncate_body )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussion( id, truncate_body );
   });
}

discussion database_api_impl::get_discussion( comment_id_type id, uint32_t truncate_body )const
{
   discussion d = _db.get( id );

   const comment_api_obj root( _db.get< comment_object, by_id >( d.root_comment ) );
   d.url = "/" + root.community + "/@" + root.author + "/" + root.permlink;
   d.root_title = root.title;
   if( root.id != d.id )
   {
      d.url += "#@" + d.author + "/" + d.permlink;
   }

   comment_interaction_state cstate = get_comment_interactions( d.author, d.permlink );
   
   d.active_votes = cstate.votes;
   d.active_views = cstate.views;
   d.active_shares = cstate.shares;
   d.active_mod_tags = cstate.moderation;
   d.body_length = d.body.size();
   
   if( truncate_body )
   {
      d.body = d.body.substr( 0, truncate_body );

      if( !fc::is_utf8( d.body ) )
      {
         d.body = fc::prune_invalid_utf8( d.body );
      }  
   }
   return d;
}

template< typename Index, typename StartItr >
vector< discussion > database_api::get_discussions( 
   const discussion_query& query,
   const string& community,
   const string& tag,
   comment_id_type parent,
   const Index& tidx,
   StartItr tidx_itr,
   uint32_t truncate_body,
   const std::function< bool( const comment_api_obj& ) >& filter,
   const std::function< bool( const comment_api_obj& ) >& exit,
   const std::function< bool( const tags::tag_object& ) >& tag_exit,
   bool ignore_parent
   )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions( query, community, tag, parent, tidx, tidx_itr, truncate_body, filter, exit, tag_exit, ignore_parent );
   });
}


template< typename Index, typename StartItr >
vector< discussion > database_api_impl::get_discussions( 
   const discussion_query& query,
   const string& community,
   const string& tag,
   comment_id_type parent,
   const Index& tidx, 
   StartItr tidx_itr,
   uint32_t truncate_body,
   const std::function< bool( const comment_api_obj& ) >& filter,
   const std::function< bool( const comment_api_obj& ) >& exit,
   const std::function< bool( const tags::tag_object& ) >& tag_exit,
   bool ignore_parent
   )const
{
   vector< discussion > results;

   if( !_db.has_index<tags::tag_index>() )
   {
      return results;
   }
      
   const auto& comment_tag_idx = _db.get_index< tags::tag_index >().indices().get< tags::by_comment >();
   const auto& gov_idx = _db.get_index< governance_subscription_index >().indices().get< by_account_governance >();
   auto gov_itr = gov_idx.begin();
   comment_id_type start;

   if( query.start_author && query.start_permlink ) 
   {
      const comment_object& start_comment = _db.get_comment( *query.start_author, *query.start_permlink );
      start = start_comment.id;
      auto comment_tag_itr = comment_tag_idx.find( start );
      
      while( comment_tag_itr != comment_tag_idx.end() && comment_tag_itr->comment == start )
      {
         if( comment_tag_itr->tag == tag && comment_tag_itr->community == community )
         {
            tidx_itr = tidx.iterator_to( *comment_tag_itr );
            break;
         }
         ++comment_tag_itr;
      }
   }

   uint32_t count = query.limit;
   uint64_t itr_count = 0;
   uint64_t filter_count = 0;
   uint64_t exc_count = 0;
   uint64_t max_itr_count = 10 * query.limit;

   while( count > 0 && tidx_itr != tidx.end() )
   {
      ++itr_count;
      if( itr_count > max_itr_count )
      {
         wlog( "Maximum iteration count exceeded serving query: ${q}", ("q", query) );
         wlog( "count=${count}   itr_count=${itr_count}   filter_count=${filter_count}   exc_count=${exc_count}",
               ("count", count)("itr_count", itr_count)("filter_count", filter_count)("exc_count", exc_count) );
         break;
      }
      if( tidx_itr->tag != tag || tidx_itr->community != community || ( !ignore_parent && tidx_itr->parent != parent ) )
      {
         break;
      } 
      try
      {
         if( !query.include_private )
         {
            if( tidx_itr->encrypted )
            {
               ++tidx_itr;
               continue;
            }
         }

         if( query.post_include_time.size() )
         {
            time_point now = _db.head_block_time();
            time_point created = tidx_itr->created;
            bool old_post = false;

            if( query.post_include_time == post_time_values[ int( post_time_type::ALL_TIME ) ] )
            {
               old_post = false;
            }
            else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_HOUR ) ] )
            {
               if( created + fc::hours(1) > now )
               {
                  old_post = true;
               }
            }
            else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_DAY ) ] )
            {
               if( created + fc::days(1) > now ) 
               {
                  old_post = true;
               }
            }
            else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_WEEK ) ] )
            {
               if( created + fc::days(7) > now ) 
               {
                  old_post = true;
               }
            }
            else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_MONTH ) ] )
            {
               if( created + fc::days(30) > now ) 
               {
                  old_post = true;
               }
            }
            else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_YEAR ) ] )
            {
               if( created + fc::days(365) > now ) 
               {
                  old_post = true;
               }
            }

            if( old_post )
            {
               ++tidx_itr;
               continue;
            }
         }
      
         if( tidx_itr->rating > query.max_rating  )
         {
            ++tidx_itr;
            continue;
         }
         
         if( query.select_authors.size() && query.select_authors.find( tidx_itr->author ) == query.select_authors.end() )
         {
            ++tidx_itr;
            continue;
         }

         if( query.select_languages.size() && query.select_languages.find( tidx_itr->language ) == query.select_languages.end() )
         {
            ++tidx_itr;
            continue;
         }

         if( query.select_communities.size() )
         {
            auto tag_itr = tidx.begin();
            auto comment_tag_itr = comment_tag_idx.find( tidx_itr->comment );
            
            if( comment_tag_itr != comment_tag_idx.end() && comment_tag_itr->comment == tidx_itr->comment )
            {
               tag_itr = tidx.iterator_to( *comment_tag_itr );
            }

            bool found = false;
            while( tag_itr != tidx.end() && tag_itr->comment == tidx_itr->comment )
            {
               if( query.select_communities.find( tag_itr->community ) != query.select_communities.end() )
               {
                  found = true; 
                  break;
               }
               ++tag_itr;
            }
            if( !found )
            {
               ++tidx_itr;
               continue;
            }
         }

         if( query.select_tags.size() )
         {
            auto tag_itr = tidx.begin();
            auto comment_tag_itr = comment_tag_idx.find( tidx_itr->comment );
            
            if( comment_tag_itr != comment_tag_idx.end() && comment_tag_itr->comment == tidx_itr->comment )
            {
               tag_itr = tidx.iterator_to( *comment_tag_itr );
            }

            bool found = false;
            while( tag_itr != tidx.end() && tag_itr->comment == tidx_itr->comment )
            {
               if( query.select_tags.find( tag_itr->tag ) != query.select_tags.end() )
               {
                  found = true;
                  break;
               }
               ++tag_itr;
            }
            if( !found )
            {
               ++tidx_itr;
               continue;
            }
         }

         if( query.filter_authors.size() && query.filter_authors.find( tidx_itr->author ) != query.filter_authors.end() )
         {
            ++tidx_itr;
            continue;
         }

         if( query.filter_languages.size() && query.filter_languages.find( tidx_itr->language ) != query.filter_languages.end() )
         {
            ++tidx_itr;
            continue;
         }

         if( query.filter_communities.size() )
         {
            auto tag_itr = tidx.begin();
            auto comment_tag_itr = comment_tag_idx.find( tidx_itr->comment );
            
            if( comment_tag_itr != comment_tag_idx.end() && comment_tag_itr->comment == tidx_itr->comment )
            {
               tag_itr = tidx.iterator_to( *comment_tag_itr );
            }

            bool found = false;
            while( tag_itr != tidx.end() && tag_itr->comment == tidx_itr->comment )
            {
               if( query.filter_communities.find( tag_itr->community ) != query.filter_communities.end() )
               {
                  found = true; 
                  break;
               }
               ++tag_itr;
            }
            if( found ) 
            {
               ++tidx_itr;
               continue;
            }
         }

         if( query.filter_tags.size() )
         {
            auto tag_itr = tidx.begin();
            auto comment_tag_itr = comment_tag_idx.find( tidx_itr->comment );
            
            if( comment_tag_itr != comment_tag_idx.end() && comment_tag_itr->comment == tidx_itr->comment )
            {
               tag_itr = tidx.iterator_to( *comment_tag_itr );
            }

            bool found = false;
            while( tag_itr != tidx.end() && tag_itr->comment == tidx_itr->comment )
            {
               if( query.filter_tags.find( tag_itr->tag ) != query.filter_tags.end() )
               {
                  found = true;
                  break;
               }
               ++tag_itr;
            }
            if( found ) 
            {
               ++tidx_itr;
               continue;
            }
         }

         discussion d = get_discussion( tidx_itr->comment, truncate_body );
         vector< moderation_state > active_mod_tags;
         vector< account_name_type > accepted_moderators;

         if( query.account != account_name_type() )
         {
            gov_itr = gov_idx.lower_bound( query.account );
            while( gov_itr != gov_idx.end() && 
               gov_itr->account == query.account )
            {
               accepted_moderators.push_back( gov_itr->governance_account );
               ++gov_itr;
            }
         }

         if( d.community != community_name_type() )
         {
            const community_member_object& community = _db.get_community_member( d.community );
            for( account_name_type mod : community.moderators )
            {
               accepted_moderators.push_back( mod );
            }
         }

         bool filtered = false;
         
         for( moderation_state m : d.active_mod_tags )
         {
            if( std::find( accepted_moderators.begin(), accepted_moderators.end(), m.moderator ) != accepted_moderators.end() )
            {
               active_mod_tags.push_back( m );
               if( m.filter )
               {
                  filtered = true;
                  break;
               }
            }
         }

         if( filtered )
         {
            ++tidx_itr;
            continue;
         }

         moderation_state init_state;
         init_state.rating = d.rating;
         active_mod_tags.push_back( init_state );        // Inject author's own rating.

         std::sort( active_mod_tags.begin(), active_mod_tags.end(), [&]( moderation_state a, moderation_state b )
         {
            return a.rating < b.rating;
         });

         d.median_rating = active_mod_tags[ active_mod_tags.size() / 2 ].rating;

         if( d.median_rating > query.max_rating )        // Exclude if median rating is greater than maximum.
         {
            ++tidx_itr;
            continue;
         }

         results.push_back( d );

         if( filter( results.back() ) )
         {
            results.pop_back();
            ++filter_count;
         }
         else if( exit( results.back() ) || tag_exit( *tidx_itr ) )
         {
            results.pop_back();
            break;
         }
         else
         {
            --count;
         }  
      }
      catch ( const fc::exception& e )
      {
         ++exc_count;
         edump((e.to_detail_string()));
      }
      ++tidx_itr;
   }
   return results;
}

comment_id_type database_api::get_parent( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_parent( query );
   });
}


comment_id_type database_api_impl::get_parent( const discussion_query& query )const
{
   comment_id_type parent;
   if( query.parent_author && query.parent_permlink ) 
   {
      parent = _db.get_comment( *query.parent_author, *query.parent_permlink ).id;
   }
   return parent;
}

vector< discussion > database_api::get_discussions_by_sort_rank( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_sort_rank( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_sort_rank( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = get_parent( query );

   string sort_option;
   string sort_time;

   if( query.sort_option.size() && query.sort_time.size() )
   {
      sort_option = query.sort_option;
      sort_time = query.sort_time;
   }

   sort_option_type option_type = sort_option_type::QUALITY_SORT;

   for( size_t i = 0; i < sort_option_values.size(); i++ )
   {
      if( sort_option == sort_option_values[ i ] )
      {
         option_type = sort_option_type( i );
         break;
      }
   }

   sort_time_type time_type = sort_time_type::STANDARD_TIME;

   for( size_t i = 0; i < sort_time_values.size(); i++ )
   {
      if( sort_time == sort_time_values[ i ] )
      {
         time_type = sort_time_type( i );
         break;
      }
   }

   switch( option_type )
   {
      case sort_option_type::QUALITY_SORT:
      {
         switch( time_type )
         {
            case sort_time_type::ACTIVE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_quality_sort_index >().indices().get< tags::by_parent_quality_active >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::RAPID_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_quality_sort_index >().indices().get< tags::by_parent_quality_rapid >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::STANDARD_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_quality_sort_index >().indices().get< tags::by_parent_quality_standard >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::TOP_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_quality_sort_index >().indices().get< tags::by_parent_quality_top >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::ELITE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_quality_sort_index >().indices().get< tags::by_parent_quality_elite >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            default:
            {
               return vector< discussion >();
            }
            break;
         }
      }
      break;
      case sort_option_type::VOTES_SORT:
      {
         switch( time_type )
         {
            case sort_time_type::ACTIVE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_votes_sort_index >().indices().get< tags::by_parent_votes_active >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::RAPID_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_votes_sort_index >().indices().get< tags::by_parent_votes_rapid >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::STANDARD_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_votes_sort_index >().indices().get< tags::by_parent_votes_standard >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::TOP_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_votes_sort_index >().indices().get< tags::by_parent_votes_top >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::ELITE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_votes_sort_index >().indices().get< tags::by_parent_votes_elite >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            default:
            {
               return vector< discussion >();
            }
            break;
         }
      }
      break;
      case sort_option_type::VIEWS_SORT:
      {
         switch( time_type )
         {
            case sort_time_type::ACTIVE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_views_sort_index >().indices().get< tags::by_parent_views_active >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::RAPID_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_views_sort_index >().indices().get< tags::by_parent_views_rapid >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::STANDARD_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_views_sort_index >().indices().get< tags::by_parent_views_standard >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::TOP_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_views_sort_index >().indices().get< tags::by_parent_views_top >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::ELITE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_views_sort_index >().indices().get< tags::by_parent_views_elite >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            default:
            {
               return vector< discussion >();
            }
            break;
         }
      }
      break;
      case sort_option_type::SHARES_SORT:
      {
         switch( time_type )
         {
            case sort_time_type::ACTIVE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_shares_sort_index >().indices().get< tags::by_parent_shares_active >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::RAPID_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_shares_sort_index >().indices().get< tags::by_parent_shares_rapid >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::STANDARD_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_shares_sort_index >().indices().get< tags::by_parent_shares_standard >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::TOP_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_shares_sort_index >().indices().get< tags::by_parent_shares_top >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::ELITE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_shares_sort_index >().indices().get< tags::by_parent_shares_elite >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            default:
            {
               return vector< discussion >();
            }
            break;
         }
      }
      break;
      case sort_option_type::COMMENTS_SORT:
      {
         switch( time_type )
         {
            case sort_time_type::ACTIVE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_comments_sort_index >().indices().get< tags::by_parent_comments_active >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::RAPID_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_comments_sort_index >().indices().get< tags::by_parent_comments_rapid >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::STANDARD_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_comments_sort_index >().indices().get< tags::by_parent_comments_standard >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::TOP_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_comments_sort_index >().indices().get< tags::by_parent_comments_top >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::ELITE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_comments_sort_index >().indices().get< tags::by_parent_comments_elite >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            default:
            {
               return vector< discussion >();
            }
            break;
         }
      }
      break;
      case sort_option_type::POPULAR_SORT:
      {
         switch( time_type )
         {
            case sort_time_type::ACTIVE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_popular_sort_index >().indices().get< tags::by_parent_popular_active >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::RAPID_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_popular_sort_index >().indices().get< tags::by_parent_popular_rapid >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::STANDARD_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_popular_sort_index >().indices().get< tags::by_parent_popular_standard >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::TOP_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_popular_sort_index >().indices().get< tags::by_parent_popular_top >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::ELITE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_popular_sort_index >().indices().get< tags::by_parent_popular_elite >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            default:
            {
               return vector< discussion >();
            }
            break;
         }
      }
      break;
      case sort_option_type::VIRAL_SORT:
      {
         switch( time_type )
         {
            case sort_time_type::ACTIVE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_viral_sort_index >().indices().get< tags::by_parent_viral_active >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::RAPID_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_viral_sort_index >().indices().get< tags::by_parent_viral_rapid >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::STANDARD_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_viral_sort_index >().indices().get< tags::by_parent_viral_standard >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::TOP_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_viral_sort_index >().indices().get< tags::by_parent_viral_top >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::ELITE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_viral_sort_index >().indices().get< tags::by_parent_viral_elite >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            default:
            {
               return vector< discussion >();
            }
            break;
         }
      }
      break;
      case sort_option_type::DISCUSSION_SORT:
      {
         switch( time_type )
         {
            case sort_time_type::ACTIVE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_discussion_sort_index >().indices().get< tags::by_parent_discussion_active >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::RAPID_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_discussion_sort_index >().indices().get< tags::by_parent_discussion_rapid >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::STANDARD_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_discussion_sort_index >().indices().get< tags::by_parent_discussion_standard >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::TOP_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_discussion_sort_index >().indices().get< tags::by_parent_discussion_top >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::ELITE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_discussion_sort_index >().indices().get< tags::by_parent_discussion_elite >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            default:
            {
               return vector< discussion >();
            }
            break;
         }
      }
      break;
      case sort_option_type::PROMINENT_SORT:
      {
         switch( time_type )
         {
            case sort_time_type::ACTIVE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_prominent_sort_index >().indices().get< tags::by_parent_prominent_active >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::RAPID_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_prominent_sort_index >().indices().get< tags::by_parent_prominent_rapid >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::STANDARD_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_prominent_sort_index >().indices().get< tags::by_parent_prominent_standard >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::TOP_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_prominent_sort_index >().indices().get< tags::by_parent_prominent_top >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::ELITE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_prominent_sort_index >().indices().get< tags::by_parent_prominent_elite >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            default:
            {
               return vector< discussion >();
            }
            break;
         }
      }
      break;
      case sort_option_type::CONVERSATION_SORT:
      {
         switch( time_type )
         {
            case sort_time_type::ACTIVE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_conversation_sort_index >().indices().get< tags::by_parent_conversation_active >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::RAPID_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_conversation_sort_index >().indices().get< tags::by_parent_conversation_rapid >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::STANDARD_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_conversation_sort_index >().indices().get< tags::by_parent_conversation_standard >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::TOP_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_conversation_sort_index >().indices().get< tags::by_parent_conversation_top >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::ELITE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_conversation_sort_index >().indices().get< tags::by_parent_conversation_elite >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            default:
            {
               return vector< discussion >();
            }
            break;
         }
      }
      break;
      case sort_option_type::DISCOURSE_SORT:
      {
         switch( time_type )
         {
            case sort_time_type::ACTIVE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_discourse_sort_index >().indices().get< tags::by_parent_discourse_active >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::RAPID_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_discourse_sort_index >().indices().get< tags::by_parent_discourse_rapid >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::STANDARD_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_discourse_sort_index >().indices().get< tags::by_parent_discourse_standard >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::TOP_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_discourse_sort_index >().indices().get< tags::by_parent_discourse_top >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            case sort_time_type::ELITE_TIME:
            {
               const auto& tag_sort_index = _db.get_index< tags::tag_discourse_sort_index >().indices().get< tags::by_parent_discourse_elite >();
               auto tag_sort_itr = tag_sort_index.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<double>::max() ) );
               return get_discussions( query, community, tag, parent, tag_sort_index, tag_sort_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
            }
            break;
            default:
            {
               return vector< discussion >();
            }
            break;
         }
      }
      break;
      default:
      {
         return vector< discussion >();
      }
      break;
   }
}


vector< discussion > database_api::get_discussions_by_feed( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_feed( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_feed( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   auto start_author = query.start_author ? *( query.start_author ) : "";
   auto start_permlink = query.start_permlink ? *( query.start_permlink ) : "";
   string account;
   if( query.account.size() )
   {
      account = query.account;
   }
   else
   {
      return vector< discussion >();
   }

   const auto& tag_idx = _db.get_index< tags::tag_index >().indices().get<tags::by_comment>();
   const auto& f_idx = _db.get_index< comment_feed_index >().indices().get< by_new_account_type >();
   auto comment_feed_itr = f_idx.lower_bound( account );
   feed_reach_type reach = feed_reach_type::FOLLOW_FEED;

   if( query.feed_type.size() )
   {
      for( size_t i = 0; i < feed_reach_values.size(); i++ )
      {
         if( query.feed_type == feed_reach_values[ i ] )
         {
            reach = feed_reach_type( i );
            break;
         }
      }

      comment_feed_itr = f_idx.lower_bound( boost::make_tuple( account, reach ) );
   }

   const auto& comment_feed_idx = _db.get_index< comment_feed_index >().indices().get< by_comment >();
   if( start_author.size() || start_permlink.size() )
   {
      const comment_object& com = _db.get_comment( start_author, start_permlink );
      auto start_c = comment_feed_idx.find( com.id );
      FC_ASSERT( start_c != comment_feed_idx.end(),
         "Comment is not in account's feed" );
      comment_feed_itr = f_idx.iterator_to( *start_c );
   }

   vector< discussion > results;
   results.reserve( query.limit );

   while( results.size() < query.limit && comment_feed_itr != f_idx.end() )
   {
      if( comment_feed_itr->account != account )
      {
         break;
      }

      if( query.post_include_time.size() )
      {
         auto tag_itr = tag_idx.lower_bound( comment_feed_itr->comment );

         time_point now = _db.head_block_time();
         time_point created = tag_itr->created;
         bool old_post = false;

         if( query.post_include_time == post_time_values[ int( post_time_type::ALL_TIME ) ] )
         {
            old_post = false;
         }
         else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_HOUR ) ] )
         {
            if( created + fc::hours(1) > now )
            {
               old_post = true;
            }
         }
         else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_DAY ) ] )
         {
            if( created + fc::days(1) > now ) 
            {
               old_post = true;
            }
         }
         else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_WEEK ) ] )
         {
            if( created + fc::days(7) > now ) 
            {
               old_post = true;
            }
         }
         else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_MONTH ) ] )
         {
            if( created + fc::days(30) > now ) 
            {
               old_post = true;
            }
         }
         else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_YEAR ) ] )
         {
            if( created + fc::days(365) > now ) 
            {
               old_post = true;
            }
         }

         if( old_post )
         {
            ++comment_feed_itr;
            continue;
         }
      }

      if( !query.include_private )
      {
         auto tag_itr = tag_idx.lower_bound( comment_feed_itr->comment );
         if( tag_itr->encrypted )
         {
            ++comment_feed_itr;
            continue;
         }
      }

      if( query.max_rating <= 9 )
      {
         auto tag_itr = tag_idx.lower_bound( comment_feed_itr->comment );

         if( tag_itr->rating > query.max_rating )
         {
            ++comment_feed_itr;
            continue;
         }
      }

      if( query.select_authors.size() )
      {
         auto tag_itr = tag_idx.lower_bound( comment_feed_itr->comment );

         if( query.select_authors.find( tag_itr->author ) == query.select_authors.end() )
         {
            ++comment_feed_itr;
            continue;
         }
      }

      if( query.select_languages.size() ) 
      {
         auto tag_itr = tag_idx.lower_bound( comment_feed_itr->comment );

         bool found = false;
         while( tag_itr != tag_idx.end() && tag_itr->comment == comment_feed_itr->comment )
         {
            if( query.select_languages.find( tag_itr->language ) != query.select_languages.end() )
            {
               found = true; 
               break;
            }
            ++tag_itr;
         }
         if( !found ) 
         {
            ++comment_feed_itr;
            continue;
         }
      }

      if( query.select_communities.size() ) 
      {
         auto tag_itr = tag_idx.lower_bound( comment_feed_itr->comment );

         bool found = false;
         while( tag_itr != tag_idx.end() && tag_itr->comment == comment_feed_itr->comment )
         {
            if( query.select_communities.find( tag_itr->community ) != query.select_communities.end() )
            {
               found = true; 
               break;
            }
            ++tag_itr;
         }
         if( !found ) 
         {
            ++comment_feed_itr;
            continue;
         }
      }

      if( query.select_tags.size() ) 
      {
         auto tag_itr = tag_idx.lower_bound( comment_feed_itr->comment );

         bool found = false;
         while( tag_itr != tag_idx.end() && tag_itr->comment == comment_feed_itr->comment )
         {
            if( query.select_tags.find( tag_itr->tag ) != query.select_tags.end() )
            {
               found = true; 
               break;
            }
            ++tag_itr;
         }
         if( !found ) 
         {
            ++comment_feed_itr;
            continue;
         }
      }

      if( query.filter_authors.size()  )
      {
         if( query.filter_authors.find( comment_feed_itr->account ) != query.filter_authors.end() )
         {
            ++comment_feed_itr;
            continue;
         }
      }

      if( query.filter_languages.size() )
      {
         auto tag_itr = tag_idx.lower_bound( comment_feed_itr->comment );

         bool found = false;
         while( tag_itr != tag_idx.end() && tag_itr->comment == comment_feed_itr->comment )
         {
            if( query.filter_languages.find( tag_itr->language ) != query.filter_languages.end() )
            {
               found = true; 
               break;
            }
            ++tag_itr;
         }
         if( found ) 
         {
            ++comment_feed_itr;
            continue;
         }
      }

      if( query.filter_communities.size() ) 
      {
         auto tag_itr = tag_idx.lower_bound( comment_feed_itr->comment );

         bool found = false;
         while( tag_itr != tag_idx.end() && tag_itr->comment == comment_feed_itr->comment )
         {
            if( query.filter_communities.find( tag_itr->community ) != query.filter_communities.end() )
            {
               found = true; 
               break;
            }
            ++tag_itr;
         }
         if( found ) 
         {
            ++comment_feed_itr;
            continue;
         }
      }

      if( query.filter_tags.size() ) 
      {
         auto tag_itr = tag_idx.lower_bound( comment_feed_itr->comment );

         bool found = false;
         while( tag_itr != tag_idx.end() && tag_itr->comment == comment_feed_itr->comment )
         {
            if( query.filter_tags.find( tag_itr->tag ) != query.filter_tags.end() )
            {
               found = true; 
               break;
            }
            ++tag_itr;
         }
         if( found ) 
         {
            ++comment_feed_itr;
            continue;
         }
      }

      try
      {
         results.push_back( get_discussion( comment_feed_itr->comment, query.truncate_body ) );
         results.back().feed = feed_api_obj( *comment_feed_itr );
      }
      catch ( const fc::exception& e )
      {
         edump((e.to_detail_string()));
      }

      ++comment_feed_itr;
   }

   return results;
}

vector< discussion > database_api::get_discussions_by_blog( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_blog( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_blog( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();

   string start_author = query.start_author ? *( query.start_author ) : "";
   string start_permlink = query.start_permlink ? *( query.start_permlink ) : "";

   string account;
   string community;
   string tag;

   if( query.account.size() )
   {
      account = query.account;
      const account_object& acc_obj = _db.get_account( account );
      if( !acc_obj.active )
      {
         return vector< discussion >();
      }
   }

   if( query.community.size() )
   {
      community = query.community;
      const community_object& community_obj = _db.get_community( community );
      if( !community_obj.active )
      {
         return vector< discussion >();
      }
   }

   if( query.tag.size() )
   {
      tag = query.tag;
   }

   blog_reach_type reach_type = blog_reach_type::ACCOUNT_BLOG;

   for( size_t i = 0; i < blog_reach_values.size(); i++ )
   {
      if( query.blog_type == blog_reach_values[ i ] )
      {
         reach_type = blog_reach_type( i );
         break;
      }
   }

   const auto& tag_idx = _db.get_index< tags::tag_index >().indices().get<tags::by_comment>();
   const auto& comment_blog_idx = _db.get_index< comment_blog_index >().indices().get< by_comment >();
   auto comment_blog_itr = comment_blog_idx.begin();

   switch( reach_type )
   {
      case blog_reach_type::ACCOUNT_BLOG:
      {
         const auto& account_comment_blog_idx = _db.get_index< comment_blog_index >().indices().get< by_new_account_blog >();
         auto comment_blog_itr = account_comment_blog_idx.lower_bound( account );
         if( start_author.size() || start_permlink.size() )
         {
            const comment_object& com = _db.get_comment( start_author, start_permlink );
            auto start_c = comment_blog_idx.find( com.id );
            FC_ASSERT( start_c != comment_blog_idx.end(),
               "Comment is not in account's blog" );
            comment_blog_itr = account_comment_blog_idx.iterator_to( *start_c );
         }
      }
      break;
      case blog_reach_type::COMMUNITY_BLOG:
      {
         const auto& community_comment_blog_idx = _db.get_index< comment_blog_index >().indices().get< by_new_community_blog >();
         auto comment_blog_itr = community_comment_blog_idx.lower_bound( community );
         if( start_author.size() || start_permlink.size() )
         {
            const comment_object& com = _db.get_comment( start_author, start_permlink );
            auto start_c = comment_blog_idx.find( com.id );
            FC_ASSERT( start_c != comment_blog_idx.end(),
               "Comment is not in community's blog" );
            comment_blog_itr = community_comment_blog_idx.iterator_to( *start_c );
         }
      }
      break;
      case blog_reach_type::TAG_BLOG:
      {
         const auto& tag_comment_blog_idx = _db.get_index< comment_blog_index >().indices().get< by_new_tag_blog >();
         auto comment_blog_itr = tag_comment_blog_idx.lower_bound( tag );
         if( start_author.size() || start_permlink.size() )
         {
            const comment_object& com = _db.get_comment( start_author, start_permlink );
            auto start_c = comment_blog_idx.find( com.id );
            FC_ASSERT( start_c != comment_blog_idx.end(),
               "Comment is not in tag's blog" );
            comment_blog_itr = tag_comment_blog_idx.iterator_to( *start_c );
         }
      }
      break;
      default:
      {
         return vector< discussion >();
      }
   }

   vector< discussion > results;
   results.reserve( query.limit );

   while( results.size() < query.limit && comment_blog_itr != comment_blog_idx.end() )
   { 
      if( account.size() && comment_blog_itr->account != account && query.blog_type == blog_reach_values[ int( blog_reach_type::ACCOUNT_BLOG ) ] )
      {
         break;
      }
      if( community.size() && comment_blog_itr->community != community && query.blog_type == blog_reach_values[ int( blog_reach_type::COMMUNITY_BLOG ) ] )
      {
         break;
      }
      if( tag.size() && comment_blog_itr->tag != tag && query.blog_type == blog_reach_values[ int( blog_reach_type::TAG_BLOG ) ] )
      {
         break;
      }

      if( query.post_include_time.size() )
      {
         auto tag_itr = tag_idx.lower_bound( comment_blog_itr->comment );

         time_point now = _db.head_block_time();
         time_point created = tag_itr->created;
         bool old_post = false;

         if( query.post_include_time == post_time_values[ int( post_time_type::ALL_TIME ) ] )
         {
            old_post = false;
         }
         else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_HOUR ) ] )
         {
            if( created + fc::hours(1) > now )
            {
               old_post = true;
            }
         }
         else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_DAY ) ] )
         {
            if( created + fc::days(1) > now ) 
            {
               old_post = true;
            }
         }
         else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_WEEK ) ] )
         {
            if( created + fc::days(7) > now ) 
            {
               old_post = true;
            }
         }
         else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_MONTH ) ] )
         {
            if( created + fc::days(30) > now ) 
            {
               old_post = true;
            }
         }
         else if( query.post_include_time == post_time_values[ int( post_time_type::LAST_YEAR ) ] )
         {
            if( created + fc::days(365) > now ) 
            {
               old_post = true;
            }
         }

         if( old_post )
         {
            ++comment_blog_itr;
            continue;
         }
      }

      if( !query.include_private )
      {
         auto tag_itr = tag_idx.lower_bound( comment_blog_itr->comment );
         if( tag_itr->encrypted )
         {
            ++comment_blog_itr;
            continue;
         }
      }

      if( query.max_rating <= 9 )
      {
         auto tag_itr = tag_idx.lower_bound( comment_blog_itr->comment );

         if( tag_itr->rating > query.max_rating )
         {
            ++comment_blog_itr;
            continue;
         }
      }

      if( query.select_authors.size() && query.select_authors.find( comment_blog_itr->account ) == query.select_authors.end() )
      {
         ++comment_blog_itr;
         continue;
      }

      if( query.select_languages.size() ) 
      {
         auto tag_itr = tag_idx.lower_bound( comment_blog_itr->comment );

         bool found = false;
         while( tag_itr != tag_idx.end() && tag_itr->comment == comment_blog_itr->comment )
         {
            if( query.select_languages.find( tag_itr->language ) != query.select_languages.end() )
            {
               found = true; 
               break;
            }
            ++tag_itr;
         }
         if( !found ) 
         {
            ++comment_blog_itr;
            continue;
         }
      }

      if( query.select_communities.size() ) 
      {
         auto tag_itr = tag_idx.lower_bound( comment_blog_itr->comment );

         bool found = false;
         while( tag_itr != tag_idx.end() && tag_itr->comment == comment_blog_itr->comment )
         {
            if( query.select_communities.find( tag_itr->community ) != query.select_communities.end() )
            {
               found = true; 
               break;
            }
            ++tag_itr;
         }
         if( !found ) 
         {
            ++comment_blog_itr;
            continue;
         }
      }

      if( query.select_tags.size() ) 
      {
         auto tag_itr = tag_idx.lower_bound( comment_blog_itr->comment );

         bool found = false;
         while( tag_itr != tag_idx.end() && tag_itr->comment == comment_blog_itr->comment )
         {
            if( query.select_tags.find( tag_itr->tag ) != query.select_tags.end() )
            {
               found = true; 
               break;
            }
            ++tag_itr;
         }
         if( !found ) 
         {
            ++comment_blog_itr;
            continue;
         }
      }

      if( query.filter_authors.size() && query.filter_authors.find( comment_blog_itr->account ) != query.filter_authors.end() )
      {
         ++comment_blog_itr;
         continue;
      }

      if( query.filter_languages.size() )
      {
         auto tag_itr = tag_idx.lower_bound( comment_blog_itr->comment );

         bool found = false;
         while( tag_itr != tag_idx.end() && tag_itr->comment == comment_blog_itr->comment )
         {
            if( query.filter_languages.find( tag_itr->language ) != query.filter_languages.end() )
            {
               found = true; 
               break;
            }
            ++tag_itr;
         }
         if( found ) 
         {
            ++comment_blog_itr;
            continue;
         }
      }

      if( query.filter_communities.size() ) 
      {
         auto tag_itr = tag_idx.lower_bound( comment_blog_itr->comment );

         bool found = false;
         while( tag_itr != tag_idx.end() && tag_itr->comment == comment_blog_itr->comment )
         {
            if( query.filter_communities.find( tag_itr->community ) != query.filter_communities.end() )
            {
               found = true; 
               break;
            }
            ++tag_itr;
         }
         if( found ) 
         {
            ++comment_blog_itr;
            continue;
         }
      }

      if( query.filter_tags.size() ) 
      {
         auto tag_itr = tag_idx.lower_bound( comment_blog_itr->comment );

         bool found = false;
         while( tag_itr != tag_idx.end() && tag_itr->comment == comment_blog_itr->comment )
         {
            if( query.filter_tags.find( tag_itr->tag ) != query.filter_tags.end() )
            {
               found = true; 
               break;
            }
            ++tag_itr;
         }
         if( found ) 
         {
            ++comment_blog_itr;
            continue;
         }
      }

      try
      {
         results.push_back( get_discussion( comment_blog_itr->comment, query.truncate_body ) );
         results.back().blog = blog_api_obj( *comment_blog_itr );
      }
      catch ( const fc::exception& e )
      {
         edump((e.to_detail_string()));
      }

      ++comment_blog_itr;
   }

   return results;
}


vector< discussion > database_api::get_discussions_by_featured( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_featured( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_featured( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = get_parent( query );

   const auto& tidx = _db.get_index<tags::tag_index>().indices().get<tags::by_parent_featured>();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag, parent, fc::time_point::maximum() ) );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, filter_default, exit_default, tag_exit_default, false );
}


vector< discussion > database_api::get_discussions_by_recommended( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_recommended( query );
   });
}

/**
 * Recommended Feed is generated with a psudeorandom 
 * ordering of posts from the authors, 
 * communities, and tags that the account has previously 
 * interacted with.
 * Pulls the top posts from each sorting index
 * of each author blog, community, and tag that the
 * account has previously interacted with.
 * Adds the top posts by each index from the highest adjacency 
 * authors, communities and tags that are currently not followed by the account.
 * Randomly pulls the limit amount of posts from
 * this set of posts.
 */
vector< discussion > database_api_impl::get_discussions_by_recommended( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   vector< discussion > results;
   results.reserve( query.limit );

   query.validate();
   if( query.account.size() )
   {
      account_name_type account = query.account;
      const auto& recommend_idx = _db.get_index< tags::account_recommendations_index >().indices().get< tags::by_account >();
      auto recommend_itr = recommend_idx.find( account );

      if( recommend_itr != recommend_idx.end() )
      {
         const tags::account_recommendations_object& aro = *recommend_itr;
         vector< comment_id_type > recommended_posts;
         recommended_posts.reserve( aro.recommended_posts.size() );

         for( auto post : aro.recommended_posts )
         {
            recommended_posts.push_back( post );
         }

         auto now_hi = uint64_t( _db.head_block_time().time_since_epoch().count() ) << 32;
         for( uint32_t i = 0; i < query.limit; ++i )
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
            uint32_t max = recommended_posts.size() - i;

            uint32_t j = i + rand % max;
            std::swap( recommended_posts[i], recommended_posts[j] );
            results.push_back( get_discussion( recommended_posts[i], query.truncate_body ) );    // Returns randomly selected comments from the recommended posts collection
         }
      }
      else
      {
         return vector< discussion >();
      }
   }
   else
   {
      return vector< discussion >();
   }

   return results;
}

vector< discussion > database_api::get_discussions_by_comments( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_comments( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_comments( const discussion_query& query )const
{
   vector< discussion > results;
   query.validate();
   FC_ASSERT( query.start_author,
      "Must get comments for a specific author" );
   string start_author = *( query.start_author );
   string start_permlink = query.start_permlink ? *( query.start_permlink ) : "";

   const auto& comment_idx = _db.get_index< comment_index >().indices().get< by_permlink >();
   const auto& t_idx = _db.get_index< comment_index >().indices().get< by_author_last_update >();
   auto comment_itr = t_idx.lower_bound( start_author );

   if( start_permlink.size() )
   {
      auto start_c = comment_idx.find( boost::make_tuple( start_author, start_permlink ) );
      FC_ASSERT( start_c != comment_idx.end(),
         "Comment is not in account's comments" );
      comment_itr = t_idx.iterator_to( *start_c );
   }

   results.reserve( query.limit );

   while( results.size() < query.limit && comment_itr != t_idx.end() )
   {
      if( comment_itr->author != start_author )
      {
         break;
      } 
      if( comment_itr->parent_author.size() > 0 )
      {
         try
         {
            results.push_back( get_discussion( comment_itr->id, query.truncate_body ) );
         }
         catch( const fc::exception& e )
         {
            edump( (e.to_detail_string() ) );
         }
      }

      ++comment_itr;
   }
   return results;
}


vector< discussion > database_api::get_discussions_by_payout( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_payout( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_payout( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = get_parent( query );

   const auto& tidx = _db.get_index< tags::tag_index >().indices().get< tags::by_net_reward >();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag ) );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
}

vector< discussion > database_api::get_post_discussions_by_payout( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_post_discussions_by_payout( query );
   });
}

vector< discussion > database_api_impl::get_post_discussions_by_payout( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = comment_id_type();    // Root posts

   const auto& tidx = _db.get_index< tags::tag_index >().indices().get< tags::by_reward_fund_net_reward >();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag, true ) );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
}

vector< discussion > database_api::get_comment_discussions_by_payout( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_comment_discussions_by_payout( query );
   });
}

vector< discussion > database_api_impl::get_comment_discussions_by_payout( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = comment_id_type(1);

   const auto& tidx = _db.get_index<tags::tag_index>().indices().get<tags::by_reward_fund_net_reward>();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag, false ) );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
}



vector< discussion > database_api::get_discussions_by_created( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_created( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_created( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = get_parent( query );

   const auto& tidx = _db.get_index<tags::tag_index>().indices().get<tags::by_parent_created>();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag, parent, fc::time_point::maximum() )  );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, filter_default, exit_default, tag_exit_default, false );
}

vector< discussion > database_api::get_discussions_by_active( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_active( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_active( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = get_parent( query );

   const auto& tidx = _db.get_index<tags::tag_index>().indices().get<tags::by_parent_active>();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag, parent, fc::time_point::maximum() )  );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, filter_default, exit_default, tag_exit_default, false );
}

vector< discussion > database_api::get_discussions_by_votes( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_votes( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_votes( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = get_parent( query );

   const auto& tidx = _db.get_index<tags::tag_index>().indices().get<tags::by_parent_net_votes>();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<int32_t>::max() ) );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, filter_default, exit_default, tag_exit_default, false );
}

vector< discussion > database_api::get_discussions_by_views( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_views( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_views( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = get_parent( query );

   const auto& tidx = _db.get_index<tags::tag_index>().indices().get<tags::by_parent_view_count>();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<int32_t>::max() )  );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, filter_default, exit_default, tag_exit_default, false );
}

vector< discussion > database_api::get_discussions_by_shares( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_shares( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_shares( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = get_parent( query );

   const auto& tidx = _db.get_index<tags::tag_index>().indices().get<tags::by_parent_share_count>();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<int32_t>::max() )  );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, filter_default, exit_default, tag_exit_default, false );
}

vector< discussion > database_api::get_discussions_by_children( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_children( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_children( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = get_parent( query );

   const auto& tidx = _db.get_index<tags::tag_index>().indices().get<tags::by_parent_children>();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<int32_t>::max() )  );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, filter_default, exit_default, tag_exit_default, false );
}

vector< discussion > database_api::get_discussions_by_vote_power( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_vote_power( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_vote_power( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = get_parent( query );

   const auto& tidx = _db.get_index<tags::tag_index>().indices().get<tags::by_parent_vote_power>();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<int32_t>::max() ) );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, filter_default, exit_default, tag_exit_default, false );
}

vector< discussion > database_api::get_discussions_by_view_power( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_view_power( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_view_power( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = get_parent( query );

   const auto& tidx = _db.get_index<tags::tag_index>().indices().get<tags::by_parent_view_power>();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<int32_t>::max() ) );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, filter_default, exit_default, tag_exit_default, false );
}


vector< discussion > database_api::get_discussions_by_share_power( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_share_power( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_share_power( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = get_parent( query );

   const auto& tidx = _db.get_index<tags::tag_index>().indices().get<tags::by_parent_share_power>();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<int32_t>::max() ) );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, filter_default, exit_default, tag_exit_default, false );
}

vector< discussion > database_api::get_discussions_by_comment_power( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_discussions_by_comment_power( query );
   });
}

vector< discussion > database_api_impl::get_discussions_by_comment_power( const discussion_query& query )const
{
   if( !_db.has_index< tags::tag_index >() )
   {
      return vector< discussion >();
   }

   query.validate();
   string community = fc::to_lower( query.community );
   string tag = fc::to_lower( query.tag );
   comment_id_type parent = get_parent( query );

   const auto& tidx = _db.get_index<tags::tag_index>().indices().get<tags::by_parent_comment_power>();
   auto tidx_itr = tidx.lower_bound( boost::make_tuple( community, tag, parent, std::numeric_limits<int32_t>::max() ) );

   return get_discussions( query, community, tag, parent, tidx, tidx_itr, query.truncate_body, filter_default, exit_default, tag_exit_default, false );
}

/**
 * This call assumes root already stored as part of state, it will
 * modify root. Replies to contain links to the reply posts and then
 * add the reply discussions to the state. This method also fetches
 * any accounts referenced by authors.
 */
void database_api::recursively_fetch_content( state& _state, discussion& root, set< string >& referenced_accounts )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->recursively_fetch_content( _state, root, referenced_accounts );
   });
}

void database_api_impl::recursively_fetch_content( state& _state, discussion& root, set< string >& referenced_accounts )const
{
   try
   {
      if( root.author.size() )
      {
         referenced_accounts.insert( root.author );
      }
      
      vector< discussion > replies = get_content_replies( root.author, root.permlink );

      for( auto& r : replies )
      {
         recursively_fetch_content( _state, r, referenced_accounts );
         root.replies.push_back( r.author + "/" + r.permlink );
         _state.content[ r.author + "/" + r.permlink ] = std::move(r);

         if( r.author.size() )
         {
            referenced_accounts.insert( r.author );
         }
      }
   }
   FC_CAPTURE_AND_RETHROW( (root.author)(root.permlink) )
}

state database_api::get_state( string path )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_state( path );
   });
}

state database_api_impl::get_state( string path )const
{
   state _state;
   _state.props = _db.get_dynamic_global_properties();
   _state.current_route = path;

   try {
      if( path.size() && path[0] == '/' )
      {
         path = path.substr(1);   // remove '/' from front
      }
      
      vector< tag_api_obj > trending_tags = get_top_tags( std::string(), 50 );

      for( const auto& t : trending_tags )
      {
         _state.tag_idx.trending.push_back( string( t.tag ) );    // Trending tags record of highest voted tags
      }

      set< string > accounts;
      vector< string > part;
      part.reserve(4);
      boost::split( part, path, boost::is_any_of("/") );
      part.resize( std::max( part.size(), size_t(4) ) );
      string account;
      string community;
      string tag;
      string section;

      for( auto item : part )
      {
         if( item[0] == '@' )
         {
            string account = fc::to_lower( item.substr(1) );
            vector< string > accvec;
            accvec.push_back( account );
            _state.accounts[ account ] = get_full_accounts( accvec )[0];
         }
         else if( item[0] == '&' )
         {
            string community = fc::to_lower( item.substr(1) );
            vector< string > communityvec;
            communityvec.push_back( community );
            _state.communities[ community ] = get_communities( communityvec )[0];
         }
         else if( item[0] == '#' )
         {
            string tag = fc::to_lower( item.substr(1) );
            vector< string > tagvec;
            tagvec.push_back( tag );
            _state.tags[ tag ] = get_account_tag_followings( tagvec )[0];
         }
         else
         {
            string section = fc::to_lower( item.substr(1) );
         }
      }

      if( section == "recent-replies" )
      {
         vector< discussion > replies = get_replies_by_last_update( account, "", 50 );
         _state.recent_replies[ account ] = vector< string >();

         for( const auto& reply : replies )
         {
            string reply_ref = reply.author + "/" + reply.permlink;
            _state.content[ reply_ref ] = reply;
            _state.recent_replies[ account ].push_back( reply_ref );
         }
      }
      else if( section == "posts" || section == "comments" )
      {
         int count = 0;
         const auto& comment_idx = _db.get_index< comment_index >().indices().get< by_author_last_update >();
         auto comment_itr = comment_idx.lower_bound( account );
         _state.comments[ account ] = vector< string >();

         while( comment_itr != comment_idx.end() && 
            comment_itr->author == account && count < 20 )
         {
            if( comment_itr->parent_author.size() )
            {
               string link = account + "/" + to_string( comment_itr->permlink );
               _state.recent_replies[ account ].push_back( link );
               _state.content[ link ] = *comment_itr;
               ++count;
            }
            ++comment_itr;
         }
      }
      else if( section == "blog" )
      {
         discussion_query q;
         q.account = account;
         q.blog_type = blog_reach_values[ int( blog_reach_type::ACCOUNT_BLOG ) ];
         vector< discussion > blog_posts = get_discussions_by_blog( q );
         _state.blogs[ account ] = vector< string >();

         for( auto b : blog_posts )
         {
            string link = b.author + "/" + b.permlink;
            _state.blogs[ account ].push_back( link );
            _state.content[ link ] = b;
         }
      }
      else if( section == "feed" )
      {
         discussion_query q;
         q.account = account;
         q.feed_type = feed_reach_values[ int( feed_reach_type::FOLLOW_FEED ) ];
         vector< discussion > feed_posts = get_discussions_by_feed( q );
         _state.blogs[ account ] = vector< string >();

         for( auto f: feed_posts )
         {
            string link = f.author + "/" + f.permlink;
            _state.feeds[ account ].push_back( link );
            _state.content[ link ] = f;
         }
      }
      else if( section == "voting_producers" || section == "~voting_producers")
      {
         vector< producer_api_obj > producers = get_producers_by_voting_power( "", 50 );
         for( const auto& p : producers )
         {
            _state.voting_producers[ p.owner ] = p;
         }
      }
      else if( section == "mining_producers" || section == "~mining_producers")
      {
         vector< producer_api_obj > producers = get_producers_by_mining_power( "", 50 );
         for( const auto& p : producers )
         {
            _state.mining_producers[ p.owner ] = p;
         }
      }
      else if( section == "communities" || section == "~communities")
      {
         vector< extended_community > communities = get_communities_by_subscribers( "", 50 );
         for( const auto& b : communities )
         {
            _state.communities[ b.name ] = b;
         }
      }
      else if( section == "payout" )
      {
         discussion_query q;
         q.community = community;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         vector< discussion > trending_disc = get_post_discussions_by_payout( q );

         for( const auto& d : trending_disc )
         {
            string key = d.author + "/" + d.permlink;
            _state.discussion_idx[ tag ].payout.push_back( key );

            if( d.author.size() )
            {
               accounts.insert( d.author );
            } 
            _state.content[ key ] = std::move( d );
         }
      }
      else if( section == "payout_comments" )
      {
         discussion_query q;
         q.community = community;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         vector< discussion > trending_disc = get_comment_discussions_by_payout( q );

         for( const auto& d : trending_disc )
         {
            string key = d.author + "/" + d.permlink;
            _state.discussion_idx[ tag ].payout_comments.push_back( key );

            if( d.author.size() )
            {
               accounts.insert( d.author );
            }
            _state.content[ key ] = std::move( d );
         }
      }
      else if( section == "responses" )
      {
         discussion_query q;
         q.community = community;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         vector< discussion > trending_disc = get_discussions_by_children( q );

         for( const auto& d : trending_disc )
         {
            auto key = d.author + "/" + d.permlink;
            _state.discussion_idx[ tag ].responses.push_back( key );

            if( d.author.size() )
            {
               accounts.insert( d.author );
            }
            _state.content[ key ] = std::move( d );
         }
      }
      else if( section == "net_votes" )
      {
         discussion_query q;
         q.community = community;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_votes( q );

         for( const auto& d : trending_disc ) 
         {
            auto key = d.author + "/" + d.permlink;
            _state.discussion_idx[ tag ].net_votes.push_back( key );

            if( d.author.size() ) 
            {
               accounts.insert (d.author );
            }
            _state.content[ key ] = std::move( d );
         }
      }
      else if( section == "view_count" )
      {
         discussion_query q;
         q.community = community;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_views( q );

         for( const auto& d : trending_disc ) 
         {
            auto key = d.author + "/" + d.permlink;
            _state.discussion_idx[ tag ].view_count.push_back( key );

            if( d.author.size() ) 
            {
               accounts.insert( d.author );
            }
            _state.content[ key ] = std::move( d );
         }
      }
      else if( section == "share_count" )
      {
         discussion_query q;
         q.community = community;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_shares( q );

         for( const auto& d : trending_disc ) 
         {
            auto key = d.author + "/" + d.permlink;
            _state.discussion_idx[ tag ].share_count.push_back( key );

            if( d.author.size() ) 
            {
               accounts.insert( d.author );
            }
            _state.content[ key ] = std::move( d );
         }
      }
      else if( section == "comment_count" )
      {
         discussion_query q;
         q.community = community;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_children( q );

         for( const auto& d : trending_disc ) 
         {
            auto key = d.author + "/" + d.permlink;
            _state.discussion_idx[ tag ].comment_count.push_back( key );

            if( d.author.size() ) 
            {
               accounts.insert( d.author );
            }
            _state.content[ key ] = std::move( d );
         }
      }
      else if( section == "vote_power" )
      {
         discussion_query q;
         q.community = community;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_vote_power( q );

         for( const auto& d : trending_disc ) 
         {
            auto key = d.author + "/" + d.permlink;
            _state.discussion_idx[ tag ].vote_power.push_back( key );

            if( d.author.size() ) 
            {
               accounts.insert( d.author );
            }
            _state.content[ key ] = std::move( d );
         }
      }
      else if( section == "view_power" )
      {
         discussion_query q;
         q.community = community;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_view_power( q );

         for( const auto& d : trending_disc ) 
         {
            auto key = d.author + "/" + d.permlink;
            _state.discussion_idx[ tag ].view_power.push_back( key );

            if( d.author.size() ) 
            {
               accounts.insert( d.author );
            }
            _state.content[ key ] = std::move( d );
         }
      }
      else if( section == "share_power" )
      {
         discussion_query q;
         q.community = community;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_share_power( q );

         for( const auto& d : trending_disc ) 
         {
            auto key = d.author + "/" + d.permlink;
            _state.discussion_idx[ tag ].share_power.push_back( key );

            if( d.author.size() ) 
            {
               accounts.insert( d.author );
            }
            _state.content[ key ] = std::move( d );
         }
      }
      else if( section == "comment_power" )
      {
         discussion_query q;
         q.community = community;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_comment_power( q );

         for( const auto& d : trending_disc ) 
         {
            auto key = d.author + "/" + d.permlink;
            _state.discussion_idx[ tag ].comment_power.push_back( key );

            if( d.author.size() ) 
            {
               accounts.insert( d.author );
            }
            _state.content[ key ] = std::move( d );
         }
      }
      else if( section == "active" )
      {
         discussion_query q;
         q.community = community;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_active( q );

         for( const auto& d : trending_disc ) 
         {
            auto key = d.author + "/" + d.permlink;
            _state.discussion_idx[ tag ].active.push_back( key );

            if( d.author.size() ) 
            {
               accounts.insert( d.author );
            }
            _state.content[ key ] = std::move( d );
         }
      }
      else if( section == "created" )
      {
         discussion_query q;
         q.community = community;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_created( q );

         for( const auto& d : trending_disc ) 
         {
            auto key = d.author + "/" + d.permlink;
            _state.discussion_idx[ tag ].created.push_back( key );

            if( d.author.size() ) 
            {
               accounts.insert( d.author );
            }
            _state.content[ key ] = std::move( d );
         }
      }
      else if( section == "tags" )
      {
         _state.tag_idx.trending.clear();
         auto trending_tags = get_top_tags( std::string(), 250 );
         for( const auto& t : trending_tags )
         {
            string name = t.tag;
            _state.tag_idx.trending.push_back( name );
            _state.tags[ name ] = account_tag_following_api_obj( _db.get_account_tag_following( name ) );
         }
      }
      else if( account.size() && section.size() )
      {
         string permlink = section;
         string key = account + "/" + permlink;
         discussion dis = get_content( account, permlink );
         recursively_fetch_content( _state, dis, accounts );
         _state.content[ key ] = std::move( dis );
      }

      for( const auto& a : accounts )
      {
         _state.accounts.erase("");
         _state.accounts[ a ] = extended_account( _db.get_account( a ), _db );
      }
      for( auto& d : _state.content ) 
      {
         comment_interaction_state cstate = get_comment_interactions( d.second.author, d.second.permlink );

         d.second.active_votes = cstate.votes;
         d.second.active_views = cstate.views;
         d.second.active_shares = cstate.shares;
         d.second.active_mod_tags = cstate.moderation;
         d.second.body_length = d.second.body.size();
      }

      _state.producer_schedule = _db.get_producer_schedule();
   }
   catch ( const fc::exception& e ) 
   {
      _state.error = e.to_detail_string();
   }
   return _state;
}


   //=======================//
   // === Subscriptions === //
   //=======================//


void database_api::set_block_applied_callback( std::function<void( const variant& block_id)> cb )
{
   my->_db.with_read_lock( [&]()
   {
      my->set_block_applied_callback( cb );
   });
}

void database_api_impl::on_applied_block( const chain::signed_block& b )
{
   try
   {
      _block_applied_callback( fc::variant(signed_block_header(b) ) );
   }
   catch( ... )
   {
      _block_applied_connection.release();
   }
}

void database_api_impl::set_block_applied_callback( std::function<void( const variant& block_header)> cb )
{
   _block_applied_callback = cb;
   _block_applied_connection = connect_signal( _db.applied_block, *this, &database_api_impl::on_applied_block );
}

} } // node::app