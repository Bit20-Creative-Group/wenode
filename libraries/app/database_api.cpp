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


class database_api_impl : public std::enable_shared_from_this<database_api_impl>
{
   public:
      database_api_impl( const node::app::api_context& ctx  );
      ~database_api_impl();

      // Subscriptions
      void set_block_applied_callback( std::function<void(const variant& block_id)> cb );

      // Blocks and transactions
      optional<block_header> get_block_header(uint32_t block_num)const;
      optional<signed_block_api_obj> get_block(uint32_t block_num)const;
      vector<applied_operation> get_ops_in_block(uint32_t block_num, bool only_virtual)const;

      // Globals
      fc::variant_object get_config()const;
      dynamic_global_property_api_obj get_dynamic_global_properties()const;

      // Keys
      vector<set<string>> get_key_references( vector<public_key_type> key )const;

      // Accounts
      vector< extended_account > get_full_accounts( vector< string > names )const;
      vector< account_api_obj > get_accounts( vector< string > names )const;
      vector< account_concise_api_obj > get_concise_accounts( vector< string > names )const;
      vector< balance_state > get_balances( vector< string > names )const;
      vector< message_state > get_messages( vector< string > names )const;

      vector<account_id_type> get_account_references( account_id_type account_id )const;
      vector<optional<account_api_obj>> lookup_account_names(const vector<string>& account_names)const;
      set<string> lookup_accounts(const string& lower_bound_name, uint32_t limit)const;
      uint64_t get_account_count()const;

      // Boards

      vector< extended_board > get_boards( vector<string> boards )const;


      // Assets

      vector< extended_asset > get_assets( vector<string> assets )const;

      // Witnesses
      vector<optional<witness_api_obj>> get_witnesses(const vector<witness_id_type>& witness_ids)const;
      fc::optional<witness_api_obj> get_witness_by_account( string account_name )const;
      set<account_name_type> lookup_witness_accounts(const string& lower_bound_name, uint32_t limit)const;
      uint64_t get_witness_count()const;

      // Market
      order_book get_order_book( uint32_t limit )const;
      vector< order_state > get_open_orders( vector< string > names )const;

      // Authority / validation
      std::string get_transaction_hex(const signed_transaction& trx)const;
      set<public_key_type> get_required_signatures( const signed_transaction& trx, const flat_set<public_key_type>& available_keys )const;
      set<public_key_type> get_potential_signatures( const signed_transaction& trx )const;
      bool verify_authority( const signed_transaction& trx )const;
      bool verify_account_authority( const string& name_or_id, const flat_set<public_key_type>& signers )const;

      // signal handlers
      void on_applied_block( const chain::signed_block& b );

      std::function<void(const fc::variant&)> _block_applied_callback;

      node::chain::database&                _db;
      std::shared_ptr< node::follow::follow_api > _follow_api;

      boost::signals2::scoped_connection       _block_applied_connection;

      bool _disable_get_block = false;
};

applied_operation::applied_operation() {}

applied_operation::applied_operation( const operation_object& op_obj )
 : trx_id( op_obj.trx_id ),
   block( op_obj.block ),
   trx_in_block( op_obj.trx_in_block ),
   op_in_trx( op_obj.op_in_trx ),
   virtual_op( op_obj.virtual_op ),
   timestamp( op_obj.timestamp )
{
   //fc::raw::unpack( op_obj.serialized_op, op );     // g++ refuses to compile this as ambiguous
   op = fc::raw::unpack< operation >( op_obj.serialized_op );
}

void find_accounts( set<string>& accounts, const discussion& d ) {
   accounts.insert( d.author );
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Subscriptions                                                    //
//                                                                  //
//////////////////////////////////////////////////////////////////////

void database_api::set_block_applied_callback( std::function<void(const variant& block_id)> cb )
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

void database_api_impl::set_block_applied_callback( std::function<void(const variant& block_header)> cb )
{
   _block_applied_callback = cb;
   _block_applied_connection = connect_signal( _db.applied_block, *this, &database_api_impl::on_applied_block );
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Constructors                                                     //
//                                                                  //
//////////////////////////////////////////////////////////////////////

database_api::database_api( const node::app::api_context& ctx )
   : my( new database_api_impl( ctx ) ) {}

database_api::~database_api() {}

database_api_impl::database_api_impl( const node::app::api_context& ctx )
   : _db( *ctx.app.chain_database() )
{
   wlog("creating database api ${x}", ("x",int64_t(this)) );

   _disable_get_block = ctx.app._disable_get_block;

   try
   {
      ctx.app.get_plugin< follow::follow_plugin >( FOLLOW_PLUGIN_NAME );
      _follow_api = std::make_shared< node::follow::follow_api >( ctx );
   }
   catch( fc::assert_exception ) { ilog("Follow Plugin not loaded"); }
}

database_api_impl::~database_api_impl()
{
   elog("freeing database api ${x}", ("x",int64_t(this)) );
}

void database_api::on_api_startup() {}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Blocks and transactions                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

optional<block_header> database_api::get_block_header(uint32_t block_num)const
{
   FC_ASSERT( !my->_disable_get_block, "get_block_header is disabled on this node." );

   return my->_db.with_read_lock( [&]()
   {
      return my->get_block_header( block_num );
   });
}

optional<block_header> database_api_impl::get_block_header(uint32_t block_num) const
{
   auto result = _db.fetch_block_by_number(block_num);
   if(result)
      return *result;
   return {};
}

optional<signed_block_api_obj> database_api::get_block(uint32_t block_num)const
{
   FC_ASSERT( !my->_disable_get_block, "get_block is disabled on this node." );

   return my->_db.with_read_lock( [&]()
   {
      return my->get_block( block_num );
   });
}

optional<signed_block_api_obj> database_api_impl::get_block(uint32_t block_num)const
{
   return _db.fetch_block_by_number(block_num);
}

vector<applied_operation> database_api::get_ops_in_block(uint32_t block_num, bool only_virtual)const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_ops_in_block( block_num, only_virtual );
   });
}

vector<applied_operation> database_api_impl::get_ops_in_block(uint32_t block_num, bool only_virtual)const
{
   const auto& idx = _db.get_index< operation_index >().indices().get< by_location >();
   auto itr = idx.lower_bound( block_num );
   vector<applied_operation> result;
   applied_operation temp;
   while( itr != idx.end() && itr->block == block_num )
   {
      temp = *itr;
      if( !only_virtual || is_virtual_operation(temp.op) )
         result.push_back(temp);
      ++itr;
   }
   return result;
}


//////////////////////////////////////////////////////////////////////
//                                                                  //
// Globals                                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////=

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

reward_fund_api_obj database_api::get_reward_fund()const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_reward_fund();
   });
}

reward_fund_api_obj database_api_impl::get_reward_fund()const
{
   return reward_fund_api_obj( _db.get( reward_fund_id_type() ), _db );
}

chain_properties database_api::get_chain_properties()const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->_db.get_witness_schedule().median_props;
   });
}

witness_schedule_api_obj database_api::get_witness_schedule()const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->_db.get(witness_schedule_id_type());
   });
}

hardfork_version database_api::get_hardfork_version()const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->_db.get(hardfork_property_id_type()).current_hardfork_version;
   });
}

scheduled_hardfork database_api::get_next_scheduled_hardfork() const
{
   return my->_db.with_read_lock( [&]()
   {
      scheduled_hardfork shf;
      const auto& hpo = my->_db.get(hardfork_property_id_type());
      shf.hf_version = hpo.next_hardfork;
      shf.live_time = hpo.next_hardfork_time;
      return shf;
   });
}



//////////////////////////////////////////////////////////////////////
//                                                                  //
// Keys                                                             //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<set<string>> database_api::get_key_references( vector<public_key_type> key )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_key_references( key );
   });
}

/**
 *  @return all accounts that referr to the key or account id in their owner or active authorities.
 */
vector<set<string>> database_api_impl::get_key_references( vector<public_key_type> keys )const
{
   FC_ASSERT( false, "database_api::get_key_references has been deprecated. Please use account_by_key_api::get_key_references instead." );
   vector< set<string> > final_result;
   return final_result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Accounts                                                         //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector< extended_account > database_api::get_full_accounts( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_full_accounts( names );
   });
}

vector< extended_account > database_api_impl::get_full_accounts( vector< string > names )const
{
   const auto& account_idx  = _db.get_index< account_index >().indices().get< by_name >();
   const auto& balance_idx = _db.get_index< account_balance_index >().indices().get< by_owner >();
   const auto& business_idx = _db.get_index< account_business_index >().indices().get< by_account >();
   const auto& following_idx = _db.get_index< account_following_index >().indices().get< by_account >();
   const auto& connection_a_idx = _db.get_index< connection_index >().indices().get< by_account_a >();
   const auto& connection_b_idx = _db.get_index< connection_index >().indices().get< by_account_b >();
   const auto& inbox_idx = _db.get_index< message_index >().indices().get< by_account_inbox >();
   const auto& outbox_idx = _db.get_index< message_index >().indices().get< by_account_outbox >();

   const auto& witness_idx = _db.get_index< witness_vote_index >().indices().get< by_account_rank >();
   const auto& executive_idx = _db.get_index< executive_board_vote_index >().indices().get< by_account_rank >();
   const auto& officer_idx = _db.get_index< network_officer_vote_index >().indices().get< by_account_type_rank >();
   const auto& enterprise_idx = _db.get_index< enterprise_approval_index >().indices().get< by_account_rank >();
   const auto& moderator_idx = _db.get_index< board_moderator_vote_index >().indices().get< by_account_board_rank >();
   const auto& account_officer_idx = _db.get_index< account_officer_vote_index >().indices().get< by_account_rank >();
   const auto& account_exec_idx = _db.get_index< account_executive_vote_index >().indices().get< by_account_rank >();

   vector< extended_account > results;

   for( auto name: names )
   {
      auto account_itr = account_idx.find( name );
      if ( account_itr != account_idx.end() )
      {
         results.push_back( extended_account( *account_itr, _db ) );

         auto balance_itr = balance_idx.lower_bound( itr->name );
         while( balance_itr != balance_idx.end() && balance_itr->owner == name )
         {
            results.back().balances[ balance_itr->symbol ] = account_balance_api_obj( *balance_itr );
            ++balance_itr;
         }

         auto following_itr = following_idx.find( name );
         if( following_itr != following_idx.end() )
         {
            results.back().following = account_following_api_obj( *following_itr );
         }

         auto business_itr = business_idx.find( name );
         if( business_itr != business_idx.end() )
         {
            results.back().business = account_business_api_obj( *business_itr );
         }

         const auto& connection_a_idx = _db.get_index< connection_index >().indices().get< by_account_a >();
         const auto& connection_b_idx = _db.get_index< connection_index >().indices().get< by_account_b >();
         auto connection_a_itr = connection_a_idx.lower_bound( boost::make_tuple( name, CONNECTION ) );
         auto connection_b_itr = connection_b_idx.lower_bound( boost::make_tuple( name, CONNECTION ) );
         while( connection_a_itr != connection_a_idx.end() && 
            connection_a_itr->account_a == name &&
            connection_a_itr->connection_type == CONNECTION )
         {
            results.back().connections[ connection_a_itr->account_b ] = connection_api_obj( *connection_a_itr, _db  );
            ++connection_a_itr;
         }
         while( connection_b_itr != connection_b_idx.end() && 
            connection_b_itr->account_b == name &&
            connection_b_itr->connection_type == CONNECTION )
         {
            results.back().connections[ connection_b_itr->account_a ] = connection_api_obj( *connection_b_itr, _db  );
            ++connection_b_itr;
         }

         auto connection_a_itr = connection_a_idx.lower_bound( boost::make_tuple( name, FRIEND ) );
         auto connection_b_itr = connection_b_idx.lower_bound( boost::make_tuple( name, FRIEND ) );
         while( connection_a_itr != connection_a_idx.end() && 
            connection_a_itr->account_a == name &&
            connection_a_itr->connection_type == FRIEND )
         {
            results.back().friends[ connection_a_itr->account_b ] = connection_api_obj( *connection_a_itr, _db  );
            ++connection_a_itr;
         }
         while( connection_b_itr != connection_b_idx.end() && 
            connection_b_itr->account_b == name &&
            connection_b_itr->connection_type == FRIEND )
         {
            results.back().friends[ connection_b_itr->account_a ] = connection_api_obj( *connection_b_itr, _db  );
            ++connection_b_itr;
         }

         auto connection_a_itr = connection_a_idx.lower_bound( boost::make_tuple( name, COMPANION ) );
         auto connection_b_itr = connection_b_idx.lower_bound( boost::make_tuple( name, COMPANION ) );
         while( connection_a_itr != connection_a_idx.end() && 
            connection_a_itr->account_a == name &&
            connection_a_itr->connection_type == COMPANION )
         {
            results.back().companions[ connection_a_itr->account_b ] = connection_api_obj( *connection_a_itr, _db );
            ++connection_a_itr;
         }
         while( connection_b_itr != connection_b_idx.end() && 
            connection_b_itr->account_b == name &&
            connection_b_itr->connection_type == COMPANION )
         {
            results.back().companions[ connection_b_itr->account_a ] = connection_api_obj( *connection_b_itr, _db );
            ++connection_b_itr;
         }

         auto inbox_itr = inbox_idx.lower_bound( name );
         auto outbox_itr = outbox_idx.lower_bound( name );
         vector< message_api_obj > inbox;
         vector< message_api_obj > outbox;
         map< account_name_type, vector< message_api_obj > > conversations;

         while( inbox_itr != inbox_idx.end() && inbox_itr->recipient == name )
         {
            inbox.push_back( message_api_obj( *inbox_itr, _db ) );
         }

         while( outbox_itr != outbox_idx.end() && outbox_itr->sender == name )
         {
            outbox.push_back( message_api_obj( *outbox_itr, _db ) );
         }

         for( auto message : inbox )
         {
            conversations[ message.sender ] = message;
         }

         for( auto message : outbox )
         {
            conversations[ message.recipient ] = message;
         }

         for( auto conv : conversations )
         {
            vector< message_api_obj > thread = conv.second;
            std::sort( thread.begin(), thread.end(), [&](auto a, auto b)
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

         auto witness_itr = witness_idx.lower_bound( name );
         while( witness_itr != witness_idx.end() && witness_itr->account == name ) 
         {
            results.back().witness_votes[ witness_itr->witness ] = witness_itr->vote_rank;
            ++witness_itr;
         }

         auto executive_itr = executive_idx.lower_bound( name );
         while( executive_itr != executive_idx.end() && executive_itr->account == name )
         {
            results.back().executive_board_votes[ executive_itr->executive_board ] = executive_itr->vote_rank;
            ++executive_itr;
         }

         auto officer_itr = officer_idx.lower_bound( name );
         while( officer_itr != officer_idx.end() && officer_itr->account == name )
         {
            results.back().network_officer_votes[ to_string( officer_itr->officer_type ) ][ officer_itr->officer_account ] = officer_itr->vote_rank;
            ++officer_itr;
         }

         auto account_exec_itr = account_exec_idx.lower_bound( name );
         while( account_exec_itr != account_exec_idx.end() && account_exec_itr->account == name )
         {
            results.back().account_executive_votes[ account_exec_itr->business_account ][ to_string( account_exec_itr->role ) ] = std::make_pair( account_exec_itr->executive_account, account_exec_itr->vote_rank );
            ++account_exec_itr;
         }

         auto account_officer_itr = account_officer_idx.lower_bound( name );
         while( account_officer_itr != account_officer_idx.end() && account_officer_itr->account == name )
         {
            results.back().account_officer_votes[ account_officer_itr->business_account ][ account_officer_itr->officer_account ] = account_officer_itr->vote_rank;
            ++account_officer_itr;
         }

         auto enterprise_itr = enterprise_idx.lower_bound( name );
         while( enterprise_itr != enterprise_idx.end() && enterprise_itr->account == name )
         {
            results.back().enterprise_approvals[ enterprise_itr->creator ][ to_string( enterprise_itr->enterprise_id ) ] = enterprise_itr->vote_rank;
            ++enterprise_itr;
         }

         auto moderator_itr = moderator_idx.lower_bound( name );
         while( moderator_itr != moderator_idx.end() && moderator_itr->account == name )
         {
            results.back().board_moderator_votes[ moderator_itr->board ][ moderator_itr->moderator ] = moderator_itr->vote_rank;
            ++moderator_itr;
         }
      }
   }

   return results;
}


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
      if ( account_itr != account_idx.end() )
      {
         results.push_back( account_api_obj( *account_itr, _db ) );
      }  
   }

   return results;
}

vector< account_concise_api_obj > database_api::get_concise_accounts( vector< string > names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_accounts( names );
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
         results.push_back( account_concise_api_obj( *account_itr, _db ) );
      }  
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
   const auto& balance_idx  = _db.get_index< account_balance_index >().indices().get< by_owner >();

   vector< balance_state > results;

   for( auto name: names )
   {
      balance_state bstate;
      auto balance_itr = balance_idx.lower_bound( name );
      while( balance_itr != balance_idx.end() && balance_itr->owner == name )
      {
         bstate.balances[ balance_itr->symbol ] = account_balance_api_obj( *balance_itr );
      }
      results.push_back( bstate );
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

   vector< balance_state > results;

   for( auto name: names )
   {
      auto inbox_itr = inbox_idx.lower_bound( name );
      auto outbox_itr = outbox_idx.lower_bound( name );
      vector< message_api_obj > inbox;
      vector< message_api_obj > outbox;
      map< account_name_type, vector< message_api_obj > > conversations;

      while( inbox_itr != inbox_idx.end() && inbox_itr->recipient == name )
      {
         inbox.push_back( message_api_obj( *inbox_itr, _db ) );
      }

      while( outbox_itr != outbox_idx.end() && outbox_itr->sender == name )
      {
         outbox.push_back( message_api_obj( *outbox_itr, _db ) );
      }

      for( auto message : inbox )
      {
         conversations[ message.sender ] = message;
      }

      for( auto message : outbox )
      {
         conversations[ message.recipient ] = message;
      }

      for( auto conv : conversations )
      {
         vector< message_api_obj > thread = conv.second;
         std::sort( thread.begin(), thread.end(), [&](auto a, auto b)
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


vector<account_id_type> database_api::get_account_references( account_id_type account_id )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_account_references( account_id );
   });
}

vector<account_id_type> database_api_impl::get_account_references( account_id_type account_id )const
{
   /*const auto& idx = _db.get_index<account_index>();
   const auto& aidx = dynamic_cast<const primary_index<account_index>&>(idx);
   const auto& refs = aidx.get_secondary_index<node::chain::account_member_index>();
   auto itr = refs.account_to_account_memberships.find(account_id);
   vector<account_id_type> result;

   if( itr != refs.account_to_account_memberships.end() )
   {
      result.reserve( itr->second.size() );
      for( auto item : itr->second ) result.push_back(item);
   }
   return result;*/
   FC_ASSERT( false, "database_api::get_account_references --- Needs to be refactored for node." );
}

vector<optional<account_api_obj>> database_api::lookup_account_names(const vector<string>& account_names)const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->lookup_account_names( account_names );
   });
}

vector<optional<account_api_obj>> database_api_impl::lookup_account_names(const vector<string>& account_names)const
{
   vector<optional<account_api_obj> > result;
   result.reserve(account_names.size());

   for( auto& name : account_names )
   {
      auto itr = _db.find< account_object, by_name >( name );

      if( itr )
      {
         result.push_back( account_api_obj( *itr, _db ) );
      }
      else
      {
         result.push_back( optional< account_api_obj>() );
      }
   }

   return result;
}

set<string> database_api::lookup_accounts(const string& lower_bound_name, uint32_t limit)const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->lookup_accounts( lower_bound_name, limit );
   });
}

set<string> database_api_impl::lookup_accounts(const string& lower_bound_name, uint32_t limit)const
{
   FC_ASSERT( limit <= 1000 );
   const auto& accounts_by_name = _db.get_index<account_index>().indices().get<by_name>();
   set<string> result;

   for( auto itr = accounts_by_name.lower_bound(lower_bound_name);
        limit-- && itr != accounts_by_name.end();
        ++itr )
   {
      result.insert(itr->name);
   }

   return result;
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
   return _db.get_index<account_index>().indices().size();
}

vector< owner_authority_history_api_obj > database_api::get_owner_history( string account )const
{
   return my->_db.with_read_lock( [&]()
   {
      vector< owner_authority_history_api_obj > results;

      const auto& hist_idx = my->_db.get_index< owner_authority_history_index >().indices().get< by_account >();
      auto itr = hist_idx.lower_bound( account );

      while( itr != hist_idx.end() && itr->account == account )
      {
         results.push_back( owner_authority_history_api_obj( *itr ) );
         ++itr;
      }

      return results;
   });
}

optional< account_recovery_request_api_obj > database_api::get_recovery_request( string account )const
{
   return my->_db.with_read_lock( [&]()
   {
      optional< account_recovery_request_api_obj > result;

      const auto& rec_idx = my->_db.get_index< account_recovery_request_index >().indices().get< by_account >();
      auto req = rec_idx.find( account );

      if( req != rec_idx.end() )
         result = account_recovery_request_api_obj( *req );

      return result;
   });
}

optional< escrow_api_obj > database_api::get_escrow( string from, uint32_t escrow_id )const
{
   return my->_db.with_read_lock( [&]()
   {
      optional< escrow_api_obj > result;

      try
      {
         result = my->_db.get_escrow( from, escrow_id );
      }
      catch ( ... ) {}

      return result;
   });
}

vector< withdraw_route > database_api::get_withdraw_routes( string account, withdraw_route_type type )const
{
   return my->_db.with_read_lock( [&]()
   {
      vector< withdraw_route > result;

      const auto& acc = my->_db.get_account( account );

      if( type == outgoing || type == all )
      {
         const auto& by_route = my->_db.get_index< unstake_asset_route_index >().indices().get< by_withdraw_route >();
         auto route = by_route.lower_bound( acc.id );

         while( route != by_route.end() && route->from_account == acc.id )
         {
            withdraw_route r;
            r.from_account = account;
            r.to_account = my->_db.get( route->to_account ).name;
            r.percent = route->percent;
            r.auto_stake = route->auto_stake;

            result.push_back( r );

            ++route;
         }
      }

      if( type == incoming || type == all )
      {
         const auto& by_dest = my->_db.get_index< unstake_asset_route_index >().indices().get< by_destination >();
         auto route = by_dest.lower_bound( acc.id );

         while( route != by_dest.end() && route->to_account == acc.id )
         {
            withdraw_route r;
            r.from_account = my->_db.get( route->from_account ).name;
            r.to_account = account;
            r.percent = route->percent;
            r.auto_stake = route->auto_stake;

            result.push_back( r );

            ++route;
         }
      }

      return result;
   });
}

optional< account_bandwidth_api_obj > database_api::get_account_bandwidth( string account, witness::bandwidth_type type )const
{
   optional< account_bandwidth_api_obj > result;

   if( my->_db.has_index< witness::account_bandwidth_index >() )
   {
      auto band = my->_db.find< witness::account_bandwidth_object, witness::by_account_bandwidth_type >( boost::make_tuple( account, type ) );
      if( band != nullptr )
         result = *band;
   }

   return result;
}



//////////////////////////////////////////////////////////////////////
//                                                                  //
// Boards                                                           //
//                                                                  //
//////////////////////////////////////////////////////////////////////



vector< extended_board > database_api::get_boards( vector< string > boards )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_boards( boards );
   });
}

vector< extended_board > database_api_impl::get_boards( vector< string > boards )const
{
   vector <extended_board > result;

   for( auto board : boards )
   {
      const auto& board_idx = _db.get_index< board_index >().indices().get< by_name >();
      const auto& board_mem_idx = _db.get_index< board_member_index >().indices().get< by_name >();
      auto board_itr = board_idx.find( board );
      if( board_itr != board_idx.end() )
      result.push_back( extended_board( *board_itr, _db ) );
      auto board_mem_itr = board_mem_idx.find( board );
      if( board_mem_itr != board_mem_idx.end() )
      {
         for( auto sub : board_mem_itr->subscribers )
         {
            result.back().subscribers.push_back( sub );
         }
         for( auto mem : board_mem_itr->members )
         {
            result.back().members.push_back( mem );
         }
         for( auto mod : board_mem_itr->moderators )
         {
            result.back().moderators.push_back( mod );
         }
         for( auto admin : board_mem_itr->administrators )
         {
            result.back().administrators.push_back( admin );
         }
         for( auto bl : board_mem_itr->blacklist )
         {
            result.back().blacklist.push_back( bl );
         }
         for( auto weight : board_mem_itr->mod_weight )
         {
            result.back().mod_weight[ weight.first ] = weight.second;
         }
         result.back().total_mod_weight = board_mem_itr->total_mod_weight;
      }
   }
   return result;
}


//////////////////////////////////////////////////////////////////////
//                                                                  //
// Assets                                                           //
//                                                                  //
//////////////////////////////////////////////////////////////////////


vector< extended_asset > database_api::get_assets( vector<string> assets )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_assets( assets );
   });
}


vector< extended_asset > database_api_impl::get_assets( vector<string> assets )const
{ 
   vector< extended_asset > result;

   for( auto asset : assets )
   {
      const auto& asset_idx = my->_db.get_index< asset_index >().indices().get< by_name >();
      const auto& asset_dyn_idx = my->_db.get_index< asset_dynamic_data_index >().indices().get< by_name >();
      auto asset_itr = asset_idx.find( asset );
      if( asset_itr != asset_idx.end() )
      result.push_back( extended_asset( *asset_itr ) );
      auto asset_dyn_itr = asset_dyn_idx.find( asset );
      if( asset_dyn_itr != asset_dyn_idx.end() )
      {
         result.back().total_supply = asset_dyn_itr->total_supply;
         result.back().liquid_supply = asset_dyn_itr->liquid_supply;
         result.back().reward_supply = asset_dyn_itr->reward_supply;
         result.back().savings_supply = asset_dyn_itr->savings_supply;
         result.back().delegated_supply = asset_dyn_itr->delegated_supply;
         result.back().receiving_supply = asset_dyn_itr->receiving_supply;
         result.back().pending_supply = asset_dyn_itr->pending_supply;
         result.back().confidential_supply = asset_dyn_itr->confidential_supply;
         result.back().accumulated_fees = asset_dyn_itr->accumulated_fees;
         result.back().fee_pool = asset_dyn_itr->fee_pool;
      }

      const auto& bitasset_idx = my->_db.get_index< asset_bitasset_data_index >().indices().get< by_symbol >();
      auto bitasset_itr = bitasset_idx.find( asset );
      if( bitasset_itr != bitasset_idx.end() )
      {
         result.back().bitasset = bitasset_data_api_obj( *bitasset_itr, _db );
      }

      const auto& equity_idx = my->_db.get_index< asset_equity_data_index >().indices().get< by_symbol >();
      auto equity_itr = equity_idx.find( asset );
      if( equity_itr != equity_idx.end() )
      {
         result.back().equity = equity_data_api_obj( *equity_itr, _db );
      }

      const auto& credit_idx = my->_db.get_index< asset_credit_data_index >().indices().get< by_symbol >();
      auto credit_itr = credit_idx.find( asset );
      if( credit_itr != credit_idx.end() )
      {
         result.back().credit = credit_data_api_obj( *credit_itr, _db );
      }

      const auto& credit_pool_idx = my->_db.get_index< asset_credit_pool_index >().indices().get< by_base_symbol >();
      auto credit_pool_itr = credit_pool_idx.find( asset );
      if( credit_pool_itr != credit_pool_idx.end() )
      {
         result.back().credit_pool = credit_pool_api_obj( *credit_pool_itr, _db );
      }

      const auto& pool_a_idx = my->_db.get_index< asset_liquidity_pool_index >().indices().get< by_symbol_a >();
      const auto& pool_b_idx = my->_db.get_index< asset_liquidity_pool_index >().indices().get< by_symbol_b >();
      auto pool_a_itr = pool_a_idx.lower_bound( asset );
      auto pool_b_itr = pool_b_idx.lower_bound( asset );

      while( pool_a_itr != pool_a_idx.end() && pool_a_itr->symbol_a == asset )
      {
         result.back().liquidity_pools[ pool_a_itr->symbol_b ] = liquidity_pool_api_obj( *pool_a_itr, _db );
      }

      while( pool_b_itr != pool_b_idx.end() && pool_b_itr->symbol_b == asset )
      {
         result.back().liquidity_pools[ pool_b_itr->symbol_a ] = liquidity_pool_api_obj( *pool_b_itr, _db );
      }
   }
   return result;
}



//////////////////////////////////////////////////////////////////////
//                                                                  //
// Witnesses                                                        //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<optional<witness_api_obj>> database_api::get_witnesses(const vector<witness_id_type>& witness_ids)const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_witnesses( witness_ids );
   });
}

vector<optional<witness_api_obj>> database_api_impl::get_witnesses(const vector<witness_id_type>& witness_ids)const
{
   vector< optional < witness_api_obj > > result; 

   result.reserve(witness_ids.size());

   std::transform( witness_ids.begin(), witness_ids.end(), std::back_inserter(result),
      [this](witness_id_type id)->optional<witness_api_obj> 
      { 
         if( auto o = _db.find(id) )
         {
            return *o;
         }
         else
         {
            return {};
         }  
      });
   return result;
}

fc::optional<witness_api_obj> database_api_impl::get_witness_by_account( string account_name )const
{
   const auto& idx = _db.get_index< witness_index >().indices().get< by_name >();
   auto itr = idx.find( account_name );
   if( itr != idx.end() )
   {
      return witness_api_obj( *itr );
   }
      
   return {};
}

fc::optional<witness_api_obj> database_api::get_witness_by_account( string account_name )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_witness_by_account( account_name );
   });
}

vector< witness_api_obj > database_api::get_witnesses_by_vote( string from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      //idump((from)(limit));
      FC_ASSERT( limit <= 100 );

      vector<witness_api_obj> result;
      result.reserve(limit);

      const auto& name_idx = my->_db.get_index< witness_index >().indices().get< by_name >();
      const auto& vote_idx = my->_db.get_index< witness_index >().indices().get< by_voting_power >();

      auto itr = vote_idx.begin();
      if( from.size() ) 
      {
         auto nameitr = name_idx.find( from );
         FC_ASSERT( nameitr != name_idx.end(), "invalid witness name ${n}", ("n",from) );
         itr = vote_idx.iterator_to( *nameitr );
      }

      while( itr != vote_idx.end() &&
            result.size() < limit &&
            itr->votes > 0 )
      {
         result.push_back( witness_api_obj( *itr ) );
         ++itr;
      }
      return result;
   });
}

vector< witness_api_obj > database_api::get_witnesses_by_mining( string from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      //idump((from)(limit));
      FC_ASSERT( limit <= 100 );

      vector<witness_api_obj> result;
      result.reserve(limit);

      const auto& name_idx = my->_db.get_index< witness_index >().indices().get< by_name >();
      const auto& vote_idx = my->_db.get_index< witness_index >().indices().get< by_mining_power >();

      auto itr = vote_idx.begin();
      if( from.size() ) 
      {
         auto nameitr = name_idx.find( from );
         FC_ASSERT( nameitr != name_idx.end(), "invalid witness name ${n}", ("n",from) );
         itr = vote_idx.iterator_to( *nameitr );
      }

      while( itr != vote_idx.end() &&
            result.size() < limit &&
            itr->votes > 0 )
      {
         result.push_back( witness_api_obj( *itr ) );
         ++itr;
      }
      return result;
   });
}



set< account_name_type > database_api::lookup_witness_accounts( const string& lower_bound_name, uint32_t limit ) const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->lookup_witness_accounts( lower_bound_name, limit );
   });
}

set< account_name_type > database_api_impl::lookup_witness_accounts( const string& lower_bound_name, uint32_t limit ) const
{
   FC_ASSERT( limit <= 1000 );
   const auto& witnesses_by_id = _db.get_index< witness_index >().indices().get< by_id >();

   // get all the names and look them all up, sort them, then figure out what
   // records to return.  This could be optimized, but we expect the
   // number of witnesses to be few and the frequency of calls to be rare
   set< account_name_type > witnesses_by_account_name;
   for ( const witness_api_obj& witness : witnesses_by_id )
      if ( witness.owner >= lower_bound_name ) // we can ignore anything below lower_bound_name
         witnesses_by_account_name.insert( witness.owner );

   auto end_iter = witnesses_by_account_name.begin();
   while ( end_iter != witnesses_by_account_name.end() && limit-- )
       ++end_iter;
   witnesses_by_account_name.erase( end_iter, witnesses_by_account_name.end() );
   return witnesses_by_account_name;
}

uint64_t database_api::get_witness_count()const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_witness_count();
   });
}

uint64_t database_api_impl::get_witness_count()const
{
   return _db.get_index<witness_index>().indices().size();
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Market                                                           //
//                                                                  //
//////////////////////////////////////////////////////////////////////


vector< order_state > database_api::get_open_orders( vector<string> names )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_open_orders( names );
   });
}

vector< order_state > database_api_impl::get_open_orders( vector<string> names )const
{
   vector< order_state > result;
   const auto& limit_idx = _db.get_index<limit_order_index>().indices().get<by_account>();
   const auto& margin_idx = _db.get_index<margin_order_index>().indices().get<by_account>();
   const auto& call_idx = _db.get_index<call_order_index>().indices().get<by_account>();
   const auto& loan_idx = _db.get_index<credit_loan_index>().indices().get<by_owner>();
   const auto& collateral_idx = _db.get_index<credit_collateral_index>().indices().get<by_owner>();

   for( auto name : names )
   {
      order_state ostate;
      auto limit_itr = limit_idx.lower_bound( name );
      while( limit_itr != limit_idx.end() && limit_itr->seller == name ) 
      {
         ostate.limit_orders.push_back( limit_order_api_obj( *limit_itr, _db ) );
         ++limit_itr;
      }

      auto margin_itr = margin_idx.lower_bound( name );
      while( margin_itr != margin_idx.end() && margin_itr->owner == name ) 
      {
         ostate.margin_orders.push_back( margin_order_api_obj( *margin_itr, _db  ) );
         ++margin_itr;
      }

      auto call_itr = call_idx.lower_bound( name );
      while( call_itr != call_idx.end() && call_itr->borrower == name ) 
      {
         ostate.call_orders.push_back( call_order_api_obj( *call_itr, _db  ) );
         ++call_itr;
      }

      auto loan_itr = loan_idx.lower_bound( name );
      while( loan_itr != loan_idx.end() && loan_itr->owner == name ) 
      {
         ostate.loan_orders.push_back( credit_loan_api_obj( *loan_itr, _db  ) );
         ++loan_itr;
      }

      auto collateral_itr = collateral_idx.lower_bound( name );
      while( collateral_itr != collateral_idx.end() && collateral_itr->owner == name ) 
      {
         ostate.collateral_orders.push_back( credit_collateral_api_obj( *collateral_itr, _db  ) );
         ++collateral_itr;
      }
   }
   return result;
}


order_book database_api::get_order_book( uint32_t limit, asset_symbol_type base, asset_symbol_type quote )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_order_book( limit, base, quote );
   });
}


order_book database_api_impl::get_order_book( uint32_t limit, asset_symbol_type base, asset_symbol_type quote )const
{
   FC_ASSERT( limit <= 1000 );
   order_book result;

   auto max_sell = price::max( base, quote );
   auto max_buy = price::max( quote, base );

   const auto& limit_price_idx = _db.get_index<limit_order_index>().indices().get<by_price>();
   const auto& margin_price_idx = _db.get_index<margin_order_index>().indices().get<by_price>();

   auto limit_sell_itr = limit_price_idx.lower_bound(max_sell);
   auto limit_buy_itr = limit_price_idx.lower_bound(max_buy);
   auto limit_end = limit_price_idx.end();

   auto margin_sell_itr = margin_price_idx.lower_bound(max_sell);
   auto margin_buy_itr = margin_price_idx.lower_bound(max_buy);
   auto margin_end = margin_price_idx.end();

//   idump((max_sell)(max_buy));
//   if( sell_itr != end ) idump((*sell_itr));
//   if( buy_itr != end ) idump((*buy_itr));

   while( ( ( limit_sell_itr != limit_end &&
       limit_sell_itr->sell_price.base.symbol == base ) ||
       ( margin_sell_itr != margin_end &&
       margin_sell_itr->sell_price.base.symbol == base ) ) && 
       result.bids.size() < limit )
   {
      if( limit_sell_itr->sell_price >= margin_sell_itr->sell_price && limit_sell_itr->sell_price.base.symbol == base )
      {
         order cur;
         auto itr = limit_sell_itr;
         cur.order_price = itr->sell_price;
         cur.real_price = (cur.order_price).to_real();
         cur.sell_asset = itr->for_sale;
         cur.buy_asset = ( asset( itr->for_sale, base ) * cur.order_price ).amount;
         cur.created = itr->created;
         result.bids.push_back( cur );
         ++limit_sell_itr;
      }
      else if( margin_sell_itr->sell_price >= limit_sell_itr->sell_price && margin_sell_itr->sell_price.base.symbol == base )
      {
         order cur;
         auto itr = margin_sell_itr;
         cur.order_price = itr->sell_price;
         cur.real_price = (cur.order_price).to_real();
         cur.sell_asset = itr->for_sale;
         cur.buy_asset = ( asset( itr->for_sale, base ) * cur.order_price ).amount;
         cur.created = itr->created;
         result.bids.push_back( cur );
         ++margin_sell_itr;
      }
      else
      {
         break;
      } 
   }
   while( ( ( limit_buy_itr != limit_end && 
      limit_buy_itr->sell_price.base.symbol == quote ) || 
      ( margin_buy_itr != margin_end && 
      margin_buy_itr->sell_price.base.symbol == quote ) ) && 
      result.asks.size() < limit )
   {
      if( limit_buy_itr->sell_price >= margin_buy_itr->sell_price && limit_buy_itr->sell_price.base.symbol == quote  )
      {
         order cur;
         auto itr = limit_buy_itr;
         cur.order_price = itr->sell_price;
         cur.real_price  = (~cur.order_price).to_real();
         cur.sell_asset = itr->for_sale;
         cur.buy_asset = ( asset( itr->for_sale, quote ) * cur.order_price ).amount;
         cur.created = itr->created;
         result.asks.push_back( cur );
         ++limit_buy_itr;
      }
      else if( margin_buy_itr->sell_price >= limit_buy_itr->sell_price && margin_buy_itr->sell_price.base.symbol == quote )
      {
         order cur;
         auto itr = margin_buy_itr;
         cur.order_price = itr->sell_price;
         cur.real_price  = (~cur.order_price).to_real();
         cur.sell_asset = itr->for_sale;
         cur.buy_asset = ( asset( itr->for_sale, quote ) * cur.order_price ).amount;
         cur.created = itr->created;
         result.asks.push_back( cur );
         ++margin_buy_itr;
      }
      else
      {
         break;
      }  
   }


   return result;
}


//////////////////////////////////////////////////////////////////////
//                                                                  //
// Authority / validation                                           //
//                                                                  //
//////////////////////////////////////////////////////////////////////

std::string database_api::get_transaction_hex(const signed_transaction& trx)const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_transaction_hex( trx );
   });
}

std::string database_api_impl::get_transaction_hex(const signed_transaction& trx)const
{
   return fc::to_hex(fc::raw::pack(trx));
}

set<public_key_type> database_api::get_required_signatures( const signed_transaction& trx, const flat_set<public_key_type>& available_keys )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_required_signatures( trx, available_keys );
   });
}

set<public_key_type> database_api_impl::get_required_signatures( const signed_transaction& trx, const flat_set<public_key_type>& available_keys )const
{
//   wdump((trx)(available_keys));
   auto result = trx.get_required_signatures( CHAIN_ID,
                                              available_keys,
                                              [&]( string account_name ){ return authority( _db.get< account_authority_object, by_account >( account_name ).active  ); },
                                              [&]( string account_name ){ return authority( _db.get< account_authority_object, by_account >( account_name ).owner   ); },
                                              [&]( string account_name ){ return authority( _db.get< account_authority_object, by_account >( account_name ).posting ); },
                                              MAX_SIG_CHECK_DEPTH );
//   wdump((result));
   return result;
}

set<public_key_type> database_api::get_potential_signatures( const signed_transaction& trx )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->get_potential_signatures( trx );
   });
}

set<public_key_type> database_api_impl::get_potential_signatures( const signed_transaction& trx )const
{
//   wdump((trx));
   set<public_key_type> result;
   trx.get_required_signatures(
      CHAIN_ID,
      flat_set<public_key_type>(),
      [&]( account_name_type account_name )
      {
         const auto& auth = _db.get< account_authority_object, by_account >(account_name).active;
         for( const auto& k : auth.get_keys() )
            result.insert(k);
         return authority( auth );
      },
      [&]( account_name_type account_name )
      {
         const auto& auth = _db.get< account_authority_object, by_account >(account_name).owner;
         for( const auto& k : auth.get_keys() )
            result.insert(k);
         return authority( auth );
      },
      [&]( account_name_type account_name )
      {
         const auto& auth = _db.get< account_authority_object, by_account >(account_name).posting;
         for( const auto& k : auth.get_keys() )
            result.insert(k);
         return authority( auth );
      },
      MAX_SIG_CHECK_DEPTH
   );

//   wdump((result));
   return result;
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
   trx.verify_authority( CHAIN_ID,
                         [&]( string account_name ){ return authority( _db.get< account_authority_object, by_account >( account_name ).active  ); },
                         [&]( string account_name ){ return authority( _db.get< account_authority_object, by_account >( account_name ).owner   ); },
                         [&]( string account_name ){ return authority( _db.get< account_authority_object, by_account >( account_name ).posting ); },
                         MAX_SIG_CHECK_DEPTH );
   return true;
}

bool database_api::verify_account_authority( const string& name_or_id, const flat_set<public_key_type>& signers )const
{
   return my->_db.with_read_lock( [&]()
   {
      return my->verify_account_authority( name_or_id, signers );
   });
}

bool database_api_impl::verify_account_authority( const string& name, const flat_set<public_key_type>& keys )const
{
   FC_ASSERT( name.size() > 0);
   auto account = _db.find< account_object, by_name >( name );
   FC_ASSERT( account, "no such account" );

   /// reuse trx.verify_authority by creating a dummy transfer
   signed_transaction trx;
   transfer_operation op;
   op.from = account->name;
   trx.operations.emplace_back(op);

   return verify_authority( trx );
}

discussion database_api::get_content( string author, string permlink )const
{
   return my->_db.with_read_lock( [&]()
   {
      const auto& by_permlink_idx = my->_db.get_index< comment_index >().indices().get< by_permlink >();
      auto itr = by_permlink_idx.find( boost::make_tuple( author, permlink ) );
      if( itr != by_permlink_idx.end() )
      {
         discussion result(*itr);
         result.active_votes = get_active_votes( author, permlink );
         result.active_views = get_active_views( author, permlink );
         result.active_shares = get_active_shares( author, permlink );
         result.active_mod_tags = get_active_mod_tags( author, permlink );
         return result;
      }
      return discussion();
   });
}

vector<vote_state> database_api::get_active_votes( string author, string permlink )const
{
   return my->_db.with_read_lock( [&]()
   {
      vector<vote_state> result;
      const auto& comment = my->_db.get_comment( author, permlink );
      const auto& idx = my->_db.get_index<comment_vote_index>().indices().get< by_comment_voter >();
      comment_id_type cid(comment.id);
      auto itr = idx.lower_bound( cid );
      while( itr != idx.end() && itr->comment == cid )
      {
         vote_state vstate;
         vstate.voter = itr->voter;
         vstate.weight = itr->weight;
         vstate.reward = itr->reward;
         vstate.percent = itr->vote_percent;
         vstate.time = itr->last_update;

         result.push_back(vstate);
         ++itr;
      }
      return result;
   });
}


vector<view_state> database_api::get_active_views( string author, string permlink )const
{
   return my->_db.with_read_lock( [&]()
   {
      vector<view_state> result;
      const auto& comment = my->_db.get_comment( author, permlink );
      const auto& idx = my->_db.get_index<comment_view_index>().indices().get< by_comment_viewer >();
      comment_id_type cid(comment.id);
      auto itr = idx.lower_bound( cid );
      while( itr != idx.end() && itr->comment == cid )
      {
         view_state vstate;
         vstate.viewer = itr->viewer;
         vstate.weight = itr->weight;
         vstate.reward = itr->reward;
         vstate.time = itr->last_update;

         result.push_back(vstate);
         ++itr;
      }
      return result;
   });
}

vector<share_state> database_api::get_active_shares( string author, string permlink )const
{
   return my->_db.with_read_lock( [&]()
   {
      vector<share_state> result;
      const auto& comment = my->_db.get_comment( author, permlink );
      const auto& idx = my->_db.get_index<comment_share_index>().indices().get< by_comment_sharer >();
      comment_id_type cid(comment.id);
      auto itr = idx.lower_bound( cid );
      while( itr != idx.end() && itr->comment == cid )
      {
         share_state sstate;
         sstate.sharer = itr->sharer;
         sstate.weight = itr->weight;
         sstate.reward = itr->reward;
         sstate.time = itr->last_update;

         result.push_back(sstate);
         ++itr;
      }
      return result;
   });
}


vector<moderation_state> database_api::get_active_mod_tags( string author, string permlink )const
{
   return my->_db.with_read_lock( [&]()
   {
      vector<moderation_state> result;
      const auto& comment = my->_db.get_comment( author, permlink );
      const auto& idx = my->_db.get_index<moderation_tag_index>().indices().get< by_comment_moderator >();
      comment_id_type cid(comment.id);
      auto itr = idx.lower_bound( cid );
      while( itr != idx.end() && itr->comment == cid )
      {
         moderation_state mstate;
         mstate.moderator = itr->moderator;
         for( auto tag : itr->tags )
         {
            mstate.tags.push_back( tag );
         }
         mstate.rating = itr->rating;
         mstate.details = itr->details;
         mstate.filter = itr->filter;
         mstate.time = itr->last_update;

         result.push_back(mstate);
         ++itr;
      }
      return result;
   });
}

vector<account_vote> database_api::get_account_votes( string voter )const
{
   return my->_db.with_read_lock( [&]()
   {
      vector<account_vote> result;

      const auto& voter_acnt = my->_db.get_account(voter);
      const auto& idx = my->_db.get_index<comment_vote_index>().indices().get< by_voter_comment >();

      account_id_type aid(voter_acnt.id);
      auto itr = idx.lower_bound( aid );
      auto end = idx.upper_bound( aid );
      while( itr != end )
      {
         const auto& vo = my->_db.get(itr->comment);
         account_vote avote;
         avote.authorperm = vo.author+"/"+to_string( vo.permlink );
         avote.weight = itr->weight;
         avote.reward = itr->reward;
         avote.percent = itr->vote_percent;
         avote.time = itr->last_update;
         result.push_back(avote);
         ++itr;
      }
      return result;
   });
}

u256 to256( const fc::uint128& t )
{
   u256 result( t.high_bits() );
   result <<= 65;
   result += t.low_bits();
   return result;
}

void database_api::set_url( discussion& d )const
{
   const comment_api_obj root( my->_db.get< comment_object, by_id >( d.root_comment ) );
   d.url = "/" + root.category + "/@" + root.author + "/" + root.permlink;
   d.root_title = root.title;
   if( root.id != d.id )
      d.url += "#@" + d.author + "/" + d.permlink;
}

vector<discussion> database_api::get_content_replies( string author, string permlink )const
{
   return my->_db.with_read_lock( [&]()
   {
      account_name_type acc_name = account_name_type( author );
      const auto& by_permlink_idx = my->_db.get_index< comment_index >().indices().get< by_parent >();
      auto itr = by_permlink_idx.find( boost::make_tuple( acc_name, permlink ) );
      vector<discussion> result;
      while( itr != by_permlink_idx.end() && itr->parent_author == author && to_string( itr->parent_permlink ) == permlink )
      {
         result.push_back( discussion( *itr ) );
         set_pending_payout( result.back() );
         ++itr;
      }
      return result;
   });
}

/**
 *  This method can be used to fetch replies to an account.
 *
 *  The first call should be (account_to_retrieve replies, "", limit)
 *  Subsequent calls should be (last_author, last_permlink, limit)
 */
vector<discussion> database_api::get_replies_by_last_update( account_name_type start_parent_author, string start_permlink, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      vector<discussion> result;

#ifndef IS_LOW_MEM
      FC_ASSERT( limit <= 100 );
      const auto& last_update_idx = my->_db.get_index< comment_index >().indices().get< by_last_update >();
      auto itr = last_update_idx.begin();
      const account_name_type* parent_author = &start_parent_author;

      if( start_permlink.size() )
      {
         const auto& comment = my->_db.get_comment( start_parent_author, start_permlink );
         itr = last_update_idx.iterator_to( comment );
         parent_author = &comment.parent_author;
      }
      else if( start_parent_author.size() )
      {
         itr = last_update_idx.lower_bound( start_parent_author );
      }

      result.reserve( limit );

      while( itr != last_update_idx.end() && result.size() < limit && itr->parent_author == *parent_author )
      {
         result.push_back( *itr );
         set_pending_payout(result.back());
         result.back().active_votes = get_active_votes( itr->author, to_string( itr->permlink ) );
         ++itr;
      }

#endif
      return result;
   });
}

map< uint32_t, applied_operation > database_api::get_account_history( string account, uint64_t from, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      FC_ASSERT( limit <= 10000, 
         "Limit of ${l} is greater than maxmimum allowed", ("l",limit) );
      FC_ASSERT( from >= limit, "From must be greater than limit" );
   
      const auto& idx = my->_db.get_index<account_history_index>().indices().get<by_account>();
      auto itr = idx.lower_bound( boost::make_tuple( account, from ) );
  
		uint32_t n = 0;
      map<uint32_t, applied_operation> result;
      
      while( true )
      {
         if( itr == idx.end() )
            break;
         if( itr->account != account )
            break;
         if( n >= limit )
            break;
         result[ itr->sequence ] = my->_db.get( itr->op );
         ++itr;
         ++n;
      }
      return result;
   });
}

vector<pair<string,uint32_t> > database_api::get_tags_used_by_author( const string& author )const {
   if( !my->_db.has_index<tags::author_tag_stats_index>() )
      return vector< pair< string, uint32_t > >();

   return my->_db.with_read_lock( [&]()
   {
      const auto* acnt = my->_db.find_account( author );
      FC_ASSERT( acnt != nullptr );
      const auto& tidx = my->_db.get_index<tags::author_tag_stats_index>().indices().get<tags::by_author_posts_tag>();
      auto itr = tidx.lower_bound( boost::make_tuple( acnt->id, 0 ) );
      vector<pair<string,uint32_t> > result;
      while( itr != tidx.end() && itr->author == acnt->id && result.size() < 1000 ) {
        result.push_back( std::make_pair(itr->tag, itr->total_posts) );
         ++itr;
      }
      return result;
   } );
}

vector<tag_api_obj> database_api::get_trending_tags( string after, uint32_t limit )const
{
   if( !my->_db.has_index<tags::tag_index>() )
      return vector< tag_api_obj >();

   return my->_db.with_read_lock( [&]()
   {
      limit = std::min( limit, uint32_t(1000) );
      vector<tag_api_obj> result;
      result.reserve( limit );

      const auto& nidx = my->_db.get_index<tags::tag_stats_index>().indices().get<tags::by_tag>();

      const auto& ridx = my->_db.get_index<tags::tag_stats_index>().indices().get<tags::by_trending>();
      auto itr = ridx.begin();
      if( after != "" && nidx.size() )
      {
         auto nitr = nidx.lower_bound( after );
         if( nitr == nidx.end() )
            itr = ridx.end();
         else
            itr = ridx.iterator_to( *nitr );
      }

      while( itr != ridx.end() && result.size() < limit )
      {
         result.push_back( tag_api_obj( *itr ) );
         ++itr;
      }
      return result;
   });
}

discussion database_api::get_discussion( comment_id_type id, uint32_t truncate_body )const
{
   discussion d = my->_db.get(id);
   set_url( d );
   
   d.active_votes = get_active_votes( d.author, d.permlink );
   d.active_views = get_active_views( d.author, d.permlink );
   d.active_shares = get_active_shares( d.author, d.permlink );
   d.active_mod_tags = get_active_mod_tags( d.author, d.permlink );

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


template<typename Index, typename StartItr>
vector<discussion> database_api::get_discussions( const discussion_query& query,
                                                  const string& board,
                                                  const string& tag,
                                                  comment_id_type parent,
                                                  const Index& tidx, 
                                                  StartItr tidx_itr,
                                                  uint32_t truncate_body,
                                                  const std::function< bool(const comment_api_obj& ) >& filter,
                                                  const std::function< bool(const comment_api_obj& ) >& exit,
                                                  const std::function< bool(const tags::tag_object& ) >& tag_exit,
                                                  bool ignore_parent
                                                  )const
{
   // idump((query));
   vector<discussion> result;

   if( !my->_db.has_index<tags::tag_index>() )
   {
      return result;
   }
      
   const auto& cidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_comment>();
   comment_id_type start;

   if( query.start_author && query.start_permlink ) 
   {
      start = my->_db.get_comment( *query.start_author, *query.start_permlink ).id;
      auto itr = cidx.find( start );
      while( itr != cidx.end() && itr->comment == start )
      {
         if( itr->tag == tag && itr->board == board ) 
         {
            tidx_itr = tidx.iterator_to( *itr );
            break;
         }
         ++itr;
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
      if( tidx_itr->tag != tag || tidx_itr->board != board || ( !ignore_parent && tidx_itr->parent != parent ) )
      {
         break;
      } 
      try
      {
         if( !query.include_private )
         {
            if( tidx_itr->privacy )
            {
               ++tidx_itr;
               continue;
            }
         }

         if( query.max_rating.size() )
         {
            auto tag_itr = tag_idx.lower_bound( tag_itr->comment );
            bool over_rating = false;

            switch( query.max_rating )
            {
               case FAMILY:
               {
                  if( tag_itr->rating == EXPLICIT || tag_itr->rating == MATURE || tag_itr->rating == GENERAL )
                  {
                     over_rating = true;
                  }
               }
               break;
               case GENERAL:
               {
                  if( tag_itr->rating == EXPLICIT || tag_itr->rating == MATURE )
                  {
                     over_rating = true;
                  }
               }
               break;
               case MATURE:
               {
                  if( tag_itr->rating == EXPLICIT )
                  {
                     over_rating = true;
                  }
               }
               break;
               default:
                  break;
            }
            
            if( over_rating )
            {
               ++blog_itr;
               continue;
            }
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

         if( query.select_boards.size() )
         {
            auto tag_itr = tidx.lower_bound( tidx_itr->comment );

            bool found = false;
            while( tag_itr != tidx.end() && tag_itr->comment == tidx_itr->comment )
            {
               if( query.select_boards.find( tag_itr->board ) != query.select_boards.end() )
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
            auto tag_itr = tidx.lower_bound( tidx_itr->comment );

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

         if( query.filter_boards.size() )
         {
            auto tag_itr = tidx_idx.lower_bound( tidx_itr->comment );

            bool found = false;
            while( tag_itr != tidx_idx.end() && tag_itr->comment == tidx_itr->comment )
            {
               if( query.filter_boards.find( tag_itr->board ) != query.filter_boards.end() )
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
            auto tag_itr = tidx_idx.lower_bound( tidx_itr->comment );

            bool found = false;
            while( tag_itr != tag_idx.end() && tag_itr->comment == tidx_itr->comment )
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

         result.push_back( get_discussion( tidx_itr->comment, truncate_body ) );

         if( filter( result.back() ) )
         {
            result.pop_back();
            ++filter_count;
         }
         else if( exit( result.back() ) || tag_exit( *tidx_itr ) )
         {
            result.pop_back();
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
   return result;
}

comment_id_type database_api::get_parent( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      comment_id_type parent;
      if( query.parent_author && query.parent_permlink ) {
         parent = my->_db.get_comment( *query.parent_author, *query.parent_permlink ).id;
      }
      return parent;
   });
}

vector<discussion> database_api::get_discussions_by_payout( const discussion_query& query )const
{
   if( !my->_db.has_index< tags::tag_index >() )
      return vector< discussion >();

   return my->_db.with_read_lock( [&]()
   {
      query.validate();
      auto board = fc::to_lower( query.board );
      auto tag = fc::to_lower( query.tag );
      auto parent = get_parent( query );

      const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_net_reward>();
      auto tidx_itr = tidx.lower_bound( board, tag );

      return get_discussions( query, board, tag, parent, tidx, tidx_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
   });
}

vector<discussion> database_api::get_post_discussions_by_payout( const discussion_query& query )const
{
   if( !my->_db.has_index< tags::tag_index >() )
      return vector< discussion >();

   return my->_db.with_read_lock( [&]()
   {
      query.validate();
      auto board = fc::to_lower( query.board );
      auto tag = fc::to_lower( query.tag );
      auto parent = comment_id_type();

      const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_reward_fund_net_reward>();
      auto tidx_itr = tidx.lower_bound( boost::make_tuple( board, tag, true ) );

      return get_discussions( query, board, tag, parent, tidx, tidx_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
   });
}

vector<discussion> database_api::get_comment_discussions_by_payout( const discussion_query& query )const
{
   if( !my->_db.has_index< tags::tag_index >() )
      return vector< discussion >();

   return my->_db.with_read_lock( [&]()
   {
      query.validate();
      auto board = fc::to_lower( query.board );
      auto tag = fc::to_lower( query.tag );
      auto parent = comment_id_type(1);

      const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_reward_fund_net_reward>();
      auto tidx_itr = tidx.lower_bound( boost::make_tuple( board, tag, false ) );

      return get_discussions( query, board, tag, parent, tidx, tidx_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward <= 0; }, exit_default, tag_exit_default, true );
   });
}

vector<discussion> database_api::get_discussions_by_index( const discussion_query& query )const
{
   if( !my->_db.has_index< tags::tag_index >() )
      return vector< discussion >();

   return my->_db.with_read_lock( [&]()
   {
      query.validate();
      auto board = fc::to_lower( query.board );
      auto tag = fc::to_lower( query.tag );
      auto parent = get_parent( query );

      string sort_type;
      string sort_time;

      if( query.sort_type.size() && query.sort_time.size() )
      {
         sort_type = query.sort_type;
         sort_time = query.sort_time;
      }

      auto tidx;

      if( sort_type == QUALITY_SORT )
      {
         if( sort_time == ACTIVE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_quality_active>();
         }
         else if( sort_time == RAPID_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_quality_rapid>();
         }
         else if( sort_time == STANDARD_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_quality_standard>();
         }
         else if( sort_time == TOP_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_quality_top>();
         }
         else if( sort_time == ELITE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_quality_elite>();
         }
      }
      else if( sort_type == VOTES_SORT )
      {
         if( sort_time == ACTIVE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_votes_active>();
         }
         else if( sort_time == RAPID_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_votes_rapid>();
         }
         else if( sort_time == STANDARD_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_votes_standard>();
         }
         else if( sort_time == TOP_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_votes_top>();
         }
         else if( sort_time == ELITE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_votes_elite>();
         }
      }
      else if( sort_type == VIEWS_SORT )
      {
         if( sort_time == ACTIVE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_views_active>();
         }
         else if( sort_time == RAPID_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_views_rapid>();
         }
         else if( sort_time == STANDARD_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_views_standard>();
         }
         else if( sort_time == TOP_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_views_top>();
         }
         else if( sort_time == ELITE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_views_elite>();
         }
      }
      else if( sort_type == SHARES_SORT )
      {
         if( sort_time == ACTIVE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_shares_active>();
         }
         else if( sort_time == RAPID_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_shares_rapid>();
         }
         else if( sort_time == STANDARD_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_shares_standard>();
         }
         else if( sort_time == TOP_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_shares_top>();
         }
         else if( sort_time == ELITE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_shares_elite>();
         }
      }
      else if( sort_type == COMMENTS_SORT )
      {
         if( sort_time == ACTIVE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_comments_active>();
         }
         else if( sort_time == RAPID_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_comments_rapid>();
         }
         else if( sort_time == STANDARD_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_comments_standard>();
         }
         else if( sort_time == TOP_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_comments_top>();
         }
         else if( sort_time == ELITE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_comments_elite>();
         }
      }
      else if( sort_type == POPULAR_SORT )
      {
         if( sort_time == ACTIVE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_popular_active>();
         }
         else if( sort_time == RAPID_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_popular_rapid>();
         }
         else if( sort_time == STANDARD_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_popular_standard>();
         }
         else if( sort_time == TOP_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_popular_top>();
         }
         else if( sort_time == ELITE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_popular_elite>();
         }
      }
      else if( sort_type == VIRAL_SORT )
      {
         if( sort_time == ACTIVE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_viral_active>();
         }
         else if( sort_time == RAPID_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_viral_rapid>();
         }
         else if( sort_time == STANDARD_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_viral_standard>();
         }
         else if( sort_time == TOP_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_viral_top>();
         }
         else if( sort_time == ELITE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_viral_elite>();
         }
      }
      else if( sort_type == DISCUSSION_SORT )
      {
         if( sort_time == ACTIVE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_discussion_active>();
         }
         else if( sort_time == RAPID_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_discussion_rapid>();
         }
         else if( sort_time == STANDARD_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_discussion_standard>();
         }
         else if( sort_time == TOP_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_discussion_top>();
         }
         else if( sort_time == ELITE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_discussion_elite>();
         }
      }
      else if( sort_type == PROMINENT_SORT )
      {
         if( sort_time == ACTIVE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_prominent_active>();
         }
         else if( sort_time == RAPID_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_prominent_rapid>();
         }
         else if( sort_time == STANDARD_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_prominent_standard>();
         }
         else if( sort_time == TOP_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_prominent_top>();
         }
         else if( sort_time == ELITE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_prominent_elite>();
         }
      }
      else if( sort_type == CONVERSATION_SORT )
      {
         if( sort_time == ACTIVE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_conversation_active>();
         }
         else if( sort_time == RAPID_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_conversation_rapid>();
         }
         else if( sort_time == STANDARD_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_conversation_standard>();
         }
         else if( sort_time == TOP_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_conversation_top>();
         }
         else if( sort_time == ELITE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_conversation_elite>();
         }
      }
      else if( sort_type == DISCOURSE_SORT )
      {
         if( sort_time == ACTIVE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_discourse_active>();
         }
         else if( sort_time == RAPID_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_discourse_rapid>();
         }
         else if( sort_time == STANDARD_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_discourse_standard>();
         }
         else if( sort_time == TOP_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_discourse_top>();
         }
         else if( sort_time == ELITE_TIME )
         {
            tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_discourse_elite>();
         }
      }

      auto tidx_itr = tidx.lower_bound( boost::make_tuple( board, tag, parent, std::numeric_limits<double>::max() )  );

      return get_discussions( query, board, tag, parent, tidx, tidx_itr, query.truncate_body, []( const comment_api_obj& c ) { return c.net_reward <= 0; } );
   });
}

vector<discussion> database_api::get_discussions_by_created( const discussion_query& query )const
{
   if( !my->_db.has_index< tags::tag_index >() )
      return vector< discussion >();

   return my->_db.with_read_lock( [&]()
   {
      query.validate();
      auto board = fc::to_lower( query.board );
      auto tag = fc::to_lower( query.tag );
      auto parent = get_parent( query );

      const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_created>();
      auto tidx_itr = tidx.lower_bound( boost::make_tuple( board, tag, parent, fc::time_point::maximum() )  );

      return get_discussions( query, board, tag, parent, tidx, tidx_itr, query.truncate_body );
   });
}

vector<discussion> database_api::get_discussions_by_active( const discussion_query& query )const
{
   if( !my->_db.has_index< tags::tag_index >() )
      return vector< discussion >();

   return my->_db.with_read_lock( [&]()
   {
      query.validate();
      auto board = fc::to_lower( query.board );
      auto tag = fc::to_lower( query.tag );
      auto parent = get_parent( query );

      const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_active>();
      auto tidx_itr = tidx.lower_bound( boost::make_tuple( board, tag, parent, fc::time_point::maximum() )  );

      return get_discussions( query, board, tag, parent, tidx, tidx_itr, query.truncate_body );
   });
}

vector<discussion> database_api::get_discussions_by_cashout( const discussion_query& query )const
{
   if( !my->_db.has_index< tags::tag_index >() )
      return vector< discussion >();

   return my->_db.with_read_lock( [&]()
   {
      query.validate();
      vector<discussion> result;
      auto board = fc::to_lower( query.board );
      auto tag = fc::to_lower( query.tag );
      auto parent = get_parent( query );

      const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_cashout>();
      auto tidx_itr = tidx.lower_bound( boost::make_tuple( board, tag, fc::time_point::now() - fc::minutes(60) ) );

      return get_discussions( query, board, tag, parent, tidx, tidx_itr, query.truncate_body, []( const comment_api_obj& c ){ return c.net_reward < 0; });
   });
}

vector<discussion> database_api::get_discussions_by_votes( const discussion_query& query )const
{
   if( !my->_db.has_index< tags::tag_index >() )
      return vector< discussion >();

   return my->_db.with_read_lock( [&]()
   {
      query.validate();
      auto board = fc::to_lower( query.board );
      auto tag = fc::to_lower( query.tag );
      auto parent = get_parent( query );

      const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_net_votes>();
      auto tidx_itr = tidx.lower_bound( boost::make_tuple( board, tag, parent, std::numeric_limits<int32_t>::max() )  );

      return get_discussions( query, board, tag, parent, tidx, tidx_itr, query.truncate_body );
   });
}

vector<discussion> database_api::get_discussions_by_views( const discussion_query& query )const
{
   if( !my->_db.has_index< tags::tag_index >() )
      return vector< discussion >();

   return my->_db.with_read_lock( [&]()
   {
      query.validate();
      auto board = fc::to_lower( query.board );
      auto tag = fc::to_lower( query.tag );
      auto parent = get_parent( query );

      const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_view_count>();
      auto tidx_itr = tidx.lower_bound( boost::make_tuple( board, tag, parent, std::numeric_limits<int32_t>::max() )  );

      return get_discussions( query, board, tag, parent, tidx, tidx_itr, query.truncate_body );
   });
}

vector<discussion> database_api::get_discussions_by_shares( const discussion_query& query )const
{
   if( !my->_db.has_index< tags::tag_index >() )
      return vector< discussion >();

   return my->_db.with_read_lock( [&]()
   {
      query.validate();
      auto board = fc::to_lower( query.board );
      auto tag = fc::to_lower( query.tag );
      auto parent = get_parent( query );

      const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_share_count>();
      auto tidx_itr = tidx.lower_bound( boost::make_tuple( board, tag, parent, std::numeric_limits<int32_t>::max() )  );

      return get_discussions( query, board, tag, parent, tidx, tidx_itr, query.truncate_body );
   });
}

vector<discussion> database_api::get_discussions_by_children( const discussion_query& query )const
{
   if( !my->_db.has_index< tags::tag_index >() )
      return vector< discussion >();

   return my->_db.with_read_lock( [&]()
   {
      query.validate();
      auto board = fc::to_lower( query.board );
      auto tag = fc::to_lower( query.tag );
      auto parent = get_parent( query );

      const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_children>();
      auto tidx_itr = tidx.lower_bound( boost::make_tuple( board, tag, parent, std::numeric_limits<int32_t>::max() )  );

      return get_discussions( query, board, tag, parent, tidx, tidx_itr, query.truncate_body );
   });
}

vector<discussion> database_api::get_discussions_by_feed( const discussion_query& query )const
{
   if( !my->_db.has_index< tags::tag_index >() )
      return vector< discussion >();

   return my->_db.with_read_lock( [&]()
   {
      query.validate();
      
      auto start_author = query.start_author ? *( query.start_author ) : "";
      auto start_permlink = query.start_permlink ? *( query.start_permlink ) : "";

      string account;
      if( query.account.size() )
      {
         account = query.account;
         const auto& acc_obj = my->_db.get_account( account );
      }
      else
      {
         return vector< discussion >();
      }
      
      const auto& f_idx = my->_db.get_index< feed_index >().indices().get< by_new_account >();
      auto feed_itr = f_idx.lower_bound( account.name );
      
      string feed_type;
      if( query.feed_type.size() )
      {
         feed_type = query.feed_type;
         f_idx = my->_db.get_index< feed_index >().indices().get< by_new_account_type >();
         feed_itr = f_idx.lower_bound( boost::make_tuple( account.name, feed_type ) );
      }

      const auto& c_idx = my->_db.get_index< feed_index >().indices().get< by_comment >();

      if( start_author.size() || start_permlink.size() )
      {
         auto start_c = c_idx.find( boost::make_tuple( my->_db.get_comment( start_author, start_permlink ).id, account.name ) );
         FC_ASSERT( start_c != c_idx.end(), "Comment is not in account's feed" );
         feed_itr = f_idx.iterator_to( *start_c );
      }

      vector< discussion > result;
      result.reserve( query.limit );

      while( result.size() < query.limit && feed_itr != f_idx.end() )
      {
         if( feed_itr->account != account.name )
         {
            break;
         }
         try
         {
            result.push_back( get_discussion( feed_itr->comment ) );
         }
         catch ( const fc::exception& e )
         {
            edump((e.to_detail_string()));
         }

         ++feed_itr;
      }
      return result;
   });
}

vector<discussion> database_api::get_discussions_by_blog( const discussion_query& query )const
{
   if( !my->_db.has_index< tags::tag_index >() )
      return vector< discussion >();

   return my->_db.with_read_lock( [&]()
   {
      query.validate();

      auto start_author = query.start_author ? *( query.start_author ) : "";
      auto start_permlink = query.start_permlink ? *( query.start_permlink ) : "";

      string account;
      string board;
      string tag;
      if( query.account.size() )
      {
         account = query.account;
         const auto& acc_obj = my->_db.get_account( account );
      }

      if( query.board.size() )
      {
         board = query.board;
         const auto& board_obj = my->_db.get_board( board );
      }

      if( query.tag.size() )
      {
         tag = query.tag;
         const auto& board_obj = my->_db.get_board( board );
      }

      const auto& tag_idx = my->_db.get_index< tags::tag_index >().indices().get<tags::by_comment>();

      const auto& c_idx = my->_db.get_index< blog_index >().indices().get< by_comment >();
      const auto& b_idx = my->_db.get_index< blog_index >().indices().get< by_new_account_blog >();

      auto blog_itr;

      string blog_type;
      if( query.blog_type.size() )
      {
         blog_type = query.blog_type;

         if( blog_type == ACCOUNT_BLOG )
         {
            b_idx = my->_db.get_index< feed_index >().indices().get< by_new_account_blog >();
            blog_itr = b_idx.lower_bound( account );
         }
         else if( blog_type == BOARD_BLOG )
         {
            b_idx = my->_db.get_index< feed_index >().indices().get< by_new_board_blog >();
            blog_itr = b_idx.lower_bound( board );
         }
         else if( blog_type == TAG_BLOG )
         {
            b_idx = my->_db.get_index< feed_index >().indices().get< by_new_tag_blog >();
            blog_itr = b_idx.lower_bound( tag );
         }
      }

      if( start_author.size() || start_permlink.size() )
      {
         auto start_c = c_idx.find( boost::make_tuple( my->_db.get_comment( start_author, start_permlink ).id, account ) );
         FC_ASSERT( start_c != c_idx.end(), "Comment is not in account's blog" );
         blog_itr = b_idx.iterator_to( *start_c );
      }

      vector< discussion > result;
      result.reserve( query.limit );

      while( result.size() < query.limit && blog_itr != b_idx.end() )
      { 
         try
         {
            if( account.size() && blog_itr->account != account && blog_type == ACCOUNT_BLOG )
            {
               break;
            }
            if( board.size() && blog_itr->board != board && blog_type == BOARD_BLOG )
            {
               break;
            }
            if( tag.size() && blog_itr->tag != tag && blog_type == TAG_BLOG )
            {
               break;
            }

            if( !query.include_private )
            {
               auto tag_itr = tag_idx.lower_bound( blog_itr->comment );
               if( tag_itr->privacy )
               {
                  ++blog_itr;
                  continue;
               }
            }

            if( query.max_rating.size() )
            {
               auto tag_itr = tag_idx.lower_bound( blog_itr->comment );
               bool over_rating = false;

               switch( query.max_rating )
               {
                  case FAMILY:
                  {
                     if( tag_itr->rating == EXPLICIT || tag_itr->rating == MATURE || tag_itr->rating == GENERAL )
                     {
                        over_rating = true;
                     }
                  }
                  break;
                  case GENERAL:
                  {
                     if( tag_itr->rating == EXPLICIT || tag_itr->rating == MATURE )
                     {
                        over_rating = true;
                     }
                  }
                  break;
                  case MATURE:
                  {
                     if( tag_itr->rating == EXPLICIT )
                     {
                        over_rating = true;
                     }
                  }
                  break;
                  default:
                     break;
               }
               
               if( over_rating )
               {
                  ++blog_itr;
                  continue;
               }
            }

            if( query.select_authors.size() && query.select_authors.find( blog_itr->account ) == query.select_authors.end() )
            {
               ++blog_itr;
               continue;
            }

            if( query.select_languages.size() ) 
            {
               auto tag_itr = tag_idx.lower_bound( blog_itr->comment );

               bool found = false;
               while( tag_itr != tag_idx.end() && tag_itr->comment == blog_itr->comment )
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
                  ++blog_itr;
                  continue;
               }
            }

            if( query.select_boards.size() ) 
            {
               auto tag_itr = tag_idx.lower_bound( blog_itr->comment );

               bool found = false;
               while( tag_itr != tag_idx.end() && tag_itr->comment == blog_itr->comment )
               {
                  if( query.select_boards.find( tag_itr->board ) != query.select_boards.end() )
                  {
                     found = true; 
                     break;
                  }
                  ++tag_itr;
               }
               if( !found ) 
               {
                  ++blog_itr;
                  continue;
               }
            }

            if( query.select_tags.size() ) 
            {
               auto tag_itr = tag_idx.lower_bound( blog_itr->comment );

               bool found = false;
               while( tag_itr != tag_idx.end() && tag_itr->comment == blog_itr->comment )
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
                  ++blog_itr;
                  continue;
               }
            }

            if( query.filter_authors.size() && query.filter_authors.find( blog_itr->account ) != query.filter_authors.end() )
            {
               ++blog_itr;
               continue;
            }

            if( query.filter_languages.size() )
            {
               auto tag_itr = tag_idx.lower_bound( blog_itr->comment );

               bool found = false;
               while( tag_itr != tag_idx.end() && tag_itr->comment == blog_itr->comment )
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
                  ++blog_itr;
                  continue;
               }
            }

            if( query.filter_boards.size() ) 
            {
               auto tag_itr = tag_idx.lower_bound( blog_itr->comment );

               bool found = false;
               while( tag_itr != tag_idx.end() && tag_itr->comment == blog_itr->comment )
               {
                  if( query.filter_boards.find( tag_itr->board ) != query.filter_boards.end() )
                  {
                     found = true; 
                     break;
                  }
                  ++tag_itr;
               }
               if( found ) 
               {
                  ++blog_itr;
                  continue;
               }
            }

            if( query.filter_tags.size() ) 
            {
               auto tag_itr = tag_idx.lower_bound( blog_itr->comment );

               bool found = false;
               while( tag_itr != tag_idx.end() && tag_itr->comment == blog_itr->comment )
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
                  ++blog_itr;
                  continue;
               }
            }
            
            result.push_back( get_discussion( blog_itr->comment, query.truncate_body ) );
         }
         catch ( const fc::exception& e )
         {
            edump((e.to_detail_string()));
         }

         ++blog_itr;
      }
      return result;
   });
}

vector<discussion> database_api::get_discussions_by_comments( const discussion_query& query )const
{
   return my->_db.with_read_lock( [&]()
   {
      vector< discussion > result;
#ifndef IS_LOW_MEM
      query.validate();
      FC_ASSERT( query.start_author, "Must get comments for a specific author" );
      auto start_author = *( query.start_author );
      auto start_permlink = query.start_permlink ? *( query.start_permlink ) : "";

      const auto& c_idx = my->_db.get_index< comment_index >().indices().get< by_permlink >();
      const auto& t_idx = my->_db.get_index< comment_index >().indices().get< by_author_last_update >();
      auto comment_itr = t_idx.lower_bound( start_author );

      if( start_permlink.size() )
      {
         auto start_c = c_idx.find( boost::make_tuple( start_author, start_permlink ) );
         FC_ASSERT( start_c != c_idx.end(), "Comment is not in account's comments" );
         comment_itr = t_idx.iterator_to( *start_c );
      }

      result.reserve( query.limit );

      while( result.size() < query.limit && comment_itr != t_idx.end() )
      {
         if( comment_itr->author != start_author )
            break;
         if( comment_itr->parent_author.size() > 0 )
         {
            try
            {
               result.push_back( get_discussion( comment_itr->id ) );
            }
            catch( const fc::exception& e )
            {
               edump( (e.to_detail_string() ) );
            }
         }

         ++comment_itr;
      }
#endif
      return result;
   });
}

/**
 *  This call assumes root already stored as part of state, it will
 *  modify root.replies to contain links to the reply posts and then
 *  add the reply discussions to the state. This method also fetches
 *  any accounts referenced by authors.
 *
 */
void database_api::recursively_fetch_content( state& _state, discussion& root, set<string>& referenced_accounts )const
{
   return my->_db.with_read_lock( [&]()
   {
      try
      {
         if( root.author.size() )
         referenced_accounts.insert(root.author);

         auto replies = get_content_replies( root.author, root.permlink );
         for( auto& r : replies )
         {
            try
            {
               recursively_fetch_content( _state, r, referenced_accounts );
               root.replies.push_back( r.author + "/" + r.permlink  );
               _state.content[r.author+"/"+r.permlink] = std::move(r);
               if( r.author.size() )
                  referenced_accounts.insert(r.author);
            }
            catch ( const fc::exception& e )
            {
               edump((e.to_detail_string()));
            }
         }
      }
      FC_CAPTURE_AND_RETHROW( (root.author)(root.permlink) )
   });
}

vector<account_name_type> database_api::get_top_miners()const
{
   return my->_db.with_read_lock( [&]()
   {
      vector<account_name_type> result;
      const auto& pow_idx = my->_db.get_index<witness_index>().indices().get<by_mining_power>();

      auto itr = pow_idx.begin();
      while( itr != pow_idx.end() && itr-> mining_power > 0  ) 
      {
         result.push_back( itr->owner );
         ++itr;
      }
      return result;
   });
}

vector< account_name_type > database_api::get_active_producers()const
{
   return my->_db.with_read_lock( [&]()
   {
      const auto& wso = my->_db.get_witness_schedule();
      size_t n = wso.current_shuffled_producers.size();
      vector< account_name_type > result;
      result.reserve( n );
      for( size_t i=0; i<n; i++ )
         result.push_back( wso.current_shuffled_producers[i] );
      return result;
   });
}

vector<discussion>  database_api::get_discussions_by_author_before_date(
    string author, string start_permlink, time_point before_date, uint32_t limit )const
{
   return my->_db.with_read_lock( [&]()
   {
      try
      {
         vector<discussion> result;
#ifndef IS_LOW_MEM
         FC_ASSERT( limit <= 100 );
         result.reserve( limit );
         uint32_t count = 0;
         const auto& didx = my->_db.get_index<comment_index>().indices().get<by_author_last_update>();

         if( before_date == time_point() )
            before_date = time_point::maximum();

         auto itr = didx.lower_bound( boost::make_tuple( author, time_point::maximum() ) );
         if( start_permlink.size() )
         {
            const auto& comment = my->_db.get_comment( author, start_permlink );
            if( comment.created < before_date )
               itr = didx.iterator_to(comment);
         }


         while( itr != didx.end() && itr->author ==  author && count < limit )
         {
            if( itr->parent_author.size() == 0 )
            {
               result.push_back( *itr );
               set_pending_payout( result.back() );
               result.back().active_votes = get_active_votes( itr->author, to_string( itr->permlink ) );
               ++count;
            }
            ++itr;
         }

#endif
         return result;
      }
      FC_CAPTURE_AND_RETHROW( (author)(start_permlink)(before_date)(limit) )
   });
}

vector< savings_withdraw_api_obj > database_api::get_savings_withdraw_from( string account )const
{
   return my->_db.with_read_lock( [&]()
   {
      vector<savings_withdraw_api_obj> result;

      const auto& from_rid_idx = my->_db.get_index< savings_withdraw_index >().indices().get< by_from_rid >();
      auto itr = from_rid_idx.lower_bound( account );
      while( itr != from_rid_idx.end() && itr->from == account ) {
         result.push_back( savings_withdraw_api_obj( *itr ) );
         ++itr;
      }
      return result;
   });
}
vector< savings_withdraw_api_obj > database_api::get_savings_withdraw_to( string account )const
{
   return my->_db.with_read_lock( [&]()
   {
      vector<savings_withdraw_api_obj> result;

      const auto& to_complete_idx = my->_db.get_index< savings_withdraw_index >().indices().get< by_to_complete >();
      auto itr = to_complete_idx.lower_bound( account );
      while( itr != to_complete_idx.end() && itr->to == account ) {
         result.push_back( savings_withdraw_api_obj( *itr ) );
         ++itr;
      }
      return result;
   });
}

vector< asset_delegation_api_obj > database_api::get_asset_delegations( string account, string from, uint32_t limit )const
{
   FC_ASSERT( limit <= 1000 );

   return my->_db.with_read_lock( [&]()
   {
      vector< asset_delegation_api_obj > result;
      result.reserve( limit );

      const auto& delegation_idx = my->_db.get_index< asset_delegation_index, by_delegation >();
      auto itr = delegation_idx.lower_bound( boost::make_tuple( account, from ) );
      while( result.size() < limit && itr != delegation_idx.end() && itr->delegator == account )
      {
         result.push_back( *itr );
         ++itr;
      }

      return result;
   });
}

vector< asset_delegation_expiration_api_obj > database_api::get_expiring_asset_delegations( string account, time_point from, uint32_t limit )const
{
   FC_ASSERT( limit <= 1000 );

   return my->_db.with_read_lock( [&]()
   {
      vector< asset_delegation_expiration_api_obj > result;
      result.reserve( limit );

      const auto& exp_idx = my->_db.get_index< asset_delegation_expiration_index, by_account_expiration >();
      auto itr = exp_idx.lower_bound( boost::make_tuple( account, from ) );
      while( result.size() < limit && itr != exp_idx.end() && itr->delegator == account )
      {
         result.push_back( *itr );
         ++itr;
      }

      return result;
   });
}

state database_api::get_state( string path )const
{
   return my->_db.with_read_lock( [&]()
   {
      state _state;
      _state.props         = get_dynamic_global_properties();
      _state.current_route = path;
      _state.feed_price    = get_current_median_history_price();

      try {
      if( path.size() && path[0] == '/' )
         path = path.substr(1); /// remove '/' from front

      if( !path.size() )
         path = "trending";

      /// FETCH CATEGORY STATE
      auto trending_tags = get_trending_tags( std::string(), 50 );
      for( const auto& t : trending_tags )
      {
         _state.tag_idx.trending.push_back( string( t.name ) );
      }
      /// END FETCH CATEGORY STATE

      set<string> accounts;

      vector<string> part; part.reserve(4);
      boost::split( part, path, boost::is_any_of("/") );
      part.resize(std::max( part.size(), size_t(4) ) ); // at least 4

      auto tag = fc::to_lower( part[1] );

      if( part[0].size() && part[0][0] == '@' ) {
         auto acnt = part[0].substr(1);
         _state.accounts[acnt] = extended_account( my->_db.get_account(acnt), my->_db );
         _state.accounts[acnt].tags_usage = get_tags_used_by_author( acnt );
         if( my->_follow_api )
         {
            _state.accounts[acnt].guest_bloggers = my->_follow_api->get_blog_authors( acnt );
            _state.accounts[acnt].reputation     = my->_follow_api->get_account_reputations( acnt, 1 )[0].reputation;
         }
         auto& eacnt = _state.accounts[acnt];
         if( part[1] == "transfers" ) {
            auto history = get_account_history( acnt, uint64_t(-1), 10000 );
            for( auto& item : history ) {
               switch( item.second.op.which() ) {
                  case operation::tag<stake_asset_operation>::value:
                  case operation::tag<unstake_asset_operation>::value:
                  case operation::tag<interest_operation>::value:
                  case operation::tag<transfer_operation>::value:
                  case operation::tag<liquidity_reward_operation>::value:
                  case operation::tag<author_reward_operation>::value:
                  case operation::tag<curation_reward_operation>::value:
                  case operation::tag<comment_benefactor_reward_operation>::value:
                  case operation::tag<transfer_to_savings_operation>::value:
                  case operation::tag<transfer_from_savings_operation>::value:
                  case operation::tag<cancel_transfer_from_savings_operation>::value:
                  case operation::tag<escrow_transfer_operation>::value:
                  case operation::tag<escrow_approve_operation>::value:
                  case operation::tag<escrow_dispute_operation>::value:
                  case operation::tag<escrow_release_operation>::value:
                  case operation::tag<fill_order_operation>::value:
                  case operation::tag<claim_reward_balance_operation>::value:
                     eacnt.transfer_history[item.first] =  item.second;
                     break;
                  case operation::tag<comment_operation>::value:
                  //   eacnt.post_history[item.first] =  item.second;
                     break;
                  case operation::tag<limit_order_create_operation>::value:
                  case operation::tag<limit_order_cancel_operation>::value:
                  //   eacnt.market_history[item.first] =  item.second;
                     break;
                  case operation::tag<vote_operation>::value:
                  case operation::tag<account_witness_vote_operation>::value:
                  case operation::tag<account_update_proxy_operation>::value:
                  //   eacnt.vote_history[item.first] =  item.second;
                     break;
                  case operation::tag<account_create_operation>::value:
                  case operation::tag<account_update_operation>::value:
                  case operation::tag<witness_update_operation>::value:
                  case operation::tag<custom_operation>::value:
                  case operation::tag<producer_reward_operation>::value:
                  default:
                     eacnt.other_history[item.first] =  item.second;
               }
            }
         } else if( part[1] == "recent-replies" ) {
         auto replies = get_replies_by_last_update( acnt, "", 50 );
         eacnt.recent_replies = vector<string>();
         for( const auto& reply : replies ) {
            auto reply_ref = reply.author+"/"+reply.permlink;
            _state.content[ reply_ref ] = reply;
            if( my->_follow_api )
            {
               _state.accounts[ reply_ref ].reputation = my->_follow_api->get_account_reputations( reply.author, 1 )[0].reputation;
            }
            eacnt.recent_replies->push_back( reply_ref );
         }
         }
         else if( part[1] == "posts" || part[1] == "comments" )
         {
   #ifndef IS_LOW_MEM
            int count = 0;
            const auto& pidx = my->_db.get_index<comment_index>().indices().get<by_author_last_update>();
            auto itr = pidx.lower_bound( acnt );
            eacnt.comments = vector<string>();

            while( itr != pidx.end() && itr->author == acnt && count < 20 )
            {
               if( itr->parent_author.size() )
               {
                  const auto link = acnt + "/" + to_string( itr->permlink );
                  eacnt.comments->push_back( link );
                  _state.content[ link ] = *itr;
                  set_pending_payout( _state.content[ link ] );
                  ++count;
               }

               ++itr;
            }
   #endif
         }
         else if( part[1].size() == 0 || part[1] == "blog" )
         {
            if( my->_follow_api )
            {
               auto blog = my->_follow_api->get_blog_entries( eacnt.name, 0, 20 );
               eacnt.blog = vector< string >();

               for( auto b: blog )
               {
                  const auto link = b.author + "/" + b.permlink;
                  eacnt.blog->push_back( link );
                  _state.content[ link ] = my->_db.get_comment( b.author, b.permlink );
                  set_pending_payout( _state.content[ link ] );

                  if( b.reblog_on > time_point() )
                  {
                     _state.content[ link ].first_reblogged_on = b.reblog_on;
                  }
               }
            }
         }
         else if( part[1].size() == 0 || part[1] == "feed" )
         {
            if( my->_follow_api )
            {
               auto feed = my->_follow_api->get_feed_entries( eacnt.name, 0, 20 );
               eacnt.feed = vector<string>();

               for( auto f: feed )
               {
                  const auto link = f.author + "/" + f.permlink;
                  eacnt.feed->push_back( link );
                  _state.content[ link ] = my->_db.get_comment( f.author, f.permlink );
                  set_pending_payout( _state.content[ link ] );
                  if( f.reblog_by.size() )
                  {
                     if( f.reblog_by.size() )
                        _state.content[link].first_reblogged_by = f.reblog_by[0];
                     _state.content[link].reblogged_by = f.reblog_by;
                     _state.content[link].first_reblogged_on = f.reblog_on;
                  }
               }
            }
         }
      }
      /// pull a complete discussion
      else if( part[1].size() && part[1][0] == '@' ) {
         auto account  = part[1].substr( 1 );
         auto slug     = part[2];

         auto key = account +"/" + slug;
         auto dis = get_content( account, slug );

         recursively_fetch_content( _state, dis, accounts );
         _state.content[key] = std::move(dis);
      }
      else if( part[0] == "witnesses" || part[0] == "~witnesses") {
         auto wits = get_witnesses_by_vote( "", 50 );
         for( const auto& w : wits ) {
            _state.witnesses[w.owner] = w;
         }
         _state.pow_queue = get_miner_queue();
      }
      else if( part[0] == "trending"  )
      {
         discussion_query q;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_trending( q );

         auto& didx = _state.discussion_idx[tag];
         for( const auto& d : trending_disc ) {
            auto key = d.author +"/" + d.permlink;
            didx.trending.push_back( key );
            if( d.author.size() ) accounts.insert(d.author);
            _state.content[key] = std::move(d);
         }
      }
      else if( part[0] == "payout"  )
      {
         discussion_query q;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_post_discussions_by_payout( q );

         auto& didx = _state.discussion_idx[tag];
         for( const auto& d : trending_disc ) {
            auto key = d.author +"/" + d.permlink;
            didx.payout.push_back( key );
            if( d.author.size() ) accounts.insert(d.author);
            _state.content[key] = std::move(d);
         }
      }
      else if( part[0] == "payout_comments"  )
      {
         discussion_query q;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_comment_discussions_by_payout( q );

         auto& didx = _state.discussion_idx[tag];
         for( const auto& d : trending_disc ) {
            auto key = d.author +"/" + d.permlink;
            didx.payout_comments.push_back( key );
            if( d.author.size() ) accounts.insert(d.author);
            _state.content[key] = std::move(d);
         }
      }
      else if( part[0] == "promoted" )
      {
         discussion_query q;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_promoted( q );

         auto& didx = _state.discussion_idx[tag];
         for( const auto& d : trending_disc )
         {
            auto key = d.author + "/" + d.permlink;
            didx.promoted.push_back( key );
            if( d.author.size() ) accounts.insert(d.author);
            _state.content[key] = std::move(d);
         }
      }
      else if( part[0] == "responses"  ) {
         discussion_query q;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_children( q );

         auto& didx = _state.discussion_idx[tag];
         for( const auto& d : trending_disc ) {
            auto key = d.author +"/" + d.permlink;
            didx.responses.push_back( key );
            if( d.author.size() ) accounts.insert(d.author);
            _state.content[key] = std::move(d);
         }
      }
      else if( !part[0].size() || part[0] == "hot" ) {
         discussion_query q;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_hot( q );

         auto& didx = _state.discussion_idx[tag];
         for( const auto& d : trending_disc ) {
            auto key = d.author +"/" + d.permlink;
            didx.hot.push_back( key );
            if( d.author.size() ) accounts.insert(d.author);
            _state.content[key] = std::move(d);
         }
      }
      else if( !part[0].size() || part[0] == "promoted" ) {
         discussion_query q;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_promoted( q );

         auto& didx = _state.discussion_idx[tag];
         for( const auto& d : trending_disc ) {
            auto key = d.author +"/" + d.permlink;
            didx.promoted.push_back( key );
            if( d.author.size() ) accounts.insert(d.author);
            _state.content[key] = std::move(d);
         }
      }
      else if( part[0] == "votes"  ) {
         discussion_query q;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_votes( q );

         auto& didx = _state.discussion_idx[tag];
         for( const auto& d : trending_disc ) {
            auto key = d.author +"/" + d.permlink;
            didx.votes.push_back( key );
            if( d.author.size() ) accounts.insert(d.author);
            _state.content[key] = std::move(d);
         }
      }
      else if( part[0] == "cashout"  ) {
         discussion_query q;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_cashout( q );

         auto& didx = _state.discussion_idx[tag];
         for( const auto& d : trending_disc ) {
            auto key = d.author +"/" + d.permlink;
            didx.cashout.push_back( key );
            if( d.author.size() ) accounts.insert(d.author);
            _state.content[key] = std::move(d);
         }
      }
      else if( part[0] == "active"  ) {
         discussion_query q;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_active( q );

         auto& didx = _state.discussion_idx[tag];
         for( const auto& d : trending_disc ) {
            auto key = d.author +"/" + d.permlink;
            didx.active.push_back( key );
            if( d.author.size() ) accounts.insert(d.author);
            _state.content[key] = std::move(d);
         }
      }
      else if( part[0] == "created"  ) {
         discussion_query q;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_created( q );

         auto& didx = _state.discussion_idx[tag];
         for( const auto& d : trending_disc ) {
            auto key = d.author +"/" + d.permlink;
            didx.created.push_back( key );
            if( d.author.size() ) accounts.insert(d.author);
            _state.content[key] = std::move(d);
         }
      }
      else if( part[0] == "recent"  ) {
         discussion_query q;
         q.tag = tag;
         q.limit = 20;
         q.truncate_body = 1024;
         auto trending_disc = get_discussions_by_created( q );

         auto& didx = _state.discussion_idx[tag];
         for( const auto& d : trending_disc ) {
            auto key = d.author +"/" + d.permlink;
            didx.created.push_back( key );
            if( d.author.size() ) accounts.insert(d.author);
            _state.content[key] = std::move(d);
         }
      }
      else if( part[0] == "tags" )
      {
         _state.tag_idx.trending.clear();
         auto trending_tags = get_trending_tags( std::string(), 250 );
         for( const auto& t : trending_tags )
         {
            string name = t.name;
            _state.tag_idx.trending.push_back( name );
            _state.tags[ name ] = t;
         }
      }
      else {
         elog( "What... no matches" );
      }

      for( const auto& a : accounts )
      {
         _state.accounts.erase("");
         _state.accounts[a] = extended_account( my->_db.get_account( a ), my->_db );
         if( my->_follow_api )
         {
            _state.accounts[a].reputation = my->_follow_api->get_account_reputations( a, 1 )[0].reputation;
         }
      }
      for( auto& d : _state.content ) {
         d.second.active_votes = get_active_votes( d.second.author, d.second.permlink );
      }

      _state.witness_schedule = my->_db.get_witness_schedule();

   } catch ( const fc::exception& e ) {
      _state.error = e.to_detail_string();
   }
   return _state;
   });
}

annotated_signed_transaction database_api::get_transaction( transaction_id_type id )const
{
#ifdef SKIP_BY_TX_ID
   FC_ASSERT( false, "This node's operator has disabled operation indexing by transaction_id" );
#else
   return my->_db.with_read_lock( [&](){
      const auto& idx = my->_db.get_index<operation_index>().indices().get<by_transaction_id>();
      auto itr = idx.lower_bound( id );
      if( itr != idx.end() && itr->trx_id == id ) {
         auto blk = my->_db.fetch_block_by_number( itr->block );
         FC_ASSERT( blk.valid() );
         FC_ASSERT( blk->transactions.size() > itr->trx_in_block );
         annotated_signed_transaction result = blk->transactions[itr->trx_in_block];
         result.block_num       = itr->block;
         result.transaction_num = itr->trx_in_block;
         return result;
      }
      FC_ASSERT( false, "Unknown Transaction ${t}", ("t",id));
   });
#endif
}


} } // node::app
