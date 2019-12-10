#include <node/protocol/node_operations.hpp>
#include <node/chain/block_summary_object.hpp>
#include <node/chain/custom_operation_interpreter.hpp>
#include <node/chain/database.hpp>
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
#include <node/chain/witness_schedule.hpp>
#include <node/witness/witness_objects.hpp>

#include <node/chain/util/asset.hpp>
#include <node/chain/util/reward.hpp>
#include <node/chain/util/uint256.hpp>
#include <node/chain/util/reward.hpp>

#include <fc/smart_ref_impl.hpp>
#include <fc/uint128.hpp>

#include <fc/container/deque.hpp>

#include <fc/io/fstream.hpp>

#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>

namespace node { namespace chain {

using boost::container::flat_set;


void database::process_asset_staking()
{
   const auto& unstake_idx = get_index< account_balance_index >().indices().get< by_next_unstake_time >();
   const auto& stake_idx = get_index< account_balance_index >().indices().get< by_next_stake_time >();
   const auto& didx = get_index< unstake_asset_route_index >().indices().get< by_withdraw_route >();
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   time_point now = props.time;
   
   auto unstake_itr = unstake_idx.begin();

   while( unstake_itr != unstake_idx.end() && unstake_itr->next_unstake_time <= now )
   {
      const account_balance_object& from_account_balance = *unstake_itr; 
      ++unstake_itr;

      share_type to_unstake;

      if ( from_account_balance.to_unstake - from_account_balance.total_unstaked < from_account_balance.unstake_rate )
      {
         to_unstake = std::min( from_account_balance.staked_balance, from_account_balance.to_unstake % from_account_balance.unstake_rate);
      }
      else
      {
         to_unstake = std::min( from_account_balance.staked_balance, from_account_balance.unstake_rate );
      }
         
      share_type total_restake = 0;
      share_type total_withdrawn = 0;

      asset unstake_asset = asset( to_unstake, from_account_balance.symbol );

      adjust_staked_balance( from_account_balance.owner, -unstake_asset );
      
      for( auto itr = didx.lower_bound( from_account_balance.owner ); itr != didx.end() && itr->from_account == from_account_balance.owner; ++itr )
      {
         if( itr->auto_stake )
         {
            share_type to_restake = (( to_unstake * itr->percent ) / PERCENT_100 );
            total_restake += to_restake;

            if( to_restake > 0 )
            {
               adjust_staked_balance( itr->to_account, to_restake );
            }
         }
         else
         {
            share_type to_withdraw = (( to_unstake * itr->percent ) / PERCENT_100 );
            total_withdrawn += to_withdraw;
            asset withdraw_asset = asset( to_withdraw, from_account_balance.symbol );

            if( to_withdraw > 0 )
            {
               adjust_liquid_balance( itr->to_account, withdraw_asset );
            }
         }
      }

      asset remaining_unstake = asset( to_unstake - total_restake - total_withdrawn, from_account_balance.symbol);
      adjust_liquid_balance( from_account_balance.owner, remaining_unstake );
         
      modify( from_account_balance, [&]( account_balance_object& abo )
      {
         abo.total_unstaked += to_unstake;

         if( abo.total_unstaked >= abo.to_unstake || abo.staked_balance == 0 )
         {
            abo.unstake_rate = 0;
            abo.next_unstake_time = fc::time_point::maximum();
         }
         else
         {
            abo.next_unstake_time += fc::seconds( STAKE_WITHDRAW_INTERVAL_SECONDS );
         }
      });
   }

   auto stake_itr = stake_idx.begin();

   while( stake_itr != stake_idx.end() && stake_itr->next_stake_time <= now )
   {
      const account_balance_object& from_account_balance = *stake_itr; 
      ++stake_itr;

      share_type to_stake;

      if ( from_account_balance.to_stake - from_account_balance.total_staked < from_account_balance.stake_rate )
      {
         to_stake = std::min( from_account_balance.staked_balance, from_account_balance.to_stake % from_account_balance.stake_rate);
      }
      else
      {
         to_stake = std::min( from_account_balance.staked_balance, from_account_balance.stake_rate );
      }
         
      share_type total_vested = 0;

      asset stake_asset = asset( to_stake, from_account_balance.symbol );

      adjust_staked_balance( from_account_balance.owner, -stake_asset );

      for( auto itr = didx.lower_bound( from_account_balance.owner ); itr != didx.end() && itr->from_account == from_account_balance.owner; ++itr )
      {
         share_type to_vest = (( to_stake * itr->percent ) / PERCENT_100 );
         total_vested += to_vest;
         asset vest_asset = asset( to_vest, from_account_balance.symbol );

         if( to_vest > 0 )
         {
            adjust_staked_balance( itr->to_account, vest_asset );
         }
      }

      asset remaining_stake = asset( to_stake - total_vested, from_account_balance.symbol);
      adjust_staked_balance( from_account_balance.owner, remaining_stake );
         
      modify( from_account_balance, [&]( account_balance_object& abo )
      {
         abo.total_staked += to_stake;

         if( abo.total_staked >= abo.to_stake || abo.liquid_balance == 0 )
         {
            abo.stake_rate = 0;
            abo.next_stake_time = fc::time_point::maximum();
         }
         else
         {
            abo.next_stake_time += fc::seconds( STAKE_WITHDRAW_INTERVAL_SECONDS );
         }
      });
   }
}

void database::process_recurring_transfers()
{
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   time_point now = props.time;
   const auto& transfer_idx = get_index< transfer_recurring_index >().indices().get< by_next_transfer >();
   auto transfer_itr = transfer_idx.begin();

   while( transfer_itr != transfer_idx.end() && transfer_itr->next_transfer <= now )
   {
      const transfer_recurring_object& transfer = *transfer_itr;
      asset liquid = get_liquid_balance( transfer.from, transfer.amount.symbol );

      if( liquid >= transfer.amount )
      {
         adjust_liquid_balance( transfer.from, -transfer.amount );
         adjust_liquid_balance( transfer.to, transfer.amount );
      }
      modify( transfer, [&]( transfer_recurring_object& tro )
      {
         tro.next_transfer+= tro.interval;
      });
   }
}

void database::process_savings_withdraws()
{
  const auto& idx = get_index< savings_withdraw_index >().indices().get< by_complete_from_rid >();
  auto itr = idx.begin();
  time_point now = head_block_time();
  while( itr != idx.end() ) 
  {
     if( itr->complete > now )
     {
        break;
     }
        
     adjust_liquid_balance( itr->to , itr->amount );

     modify( get_account( itr->from ), [&]( account_object& a )
     {
        a.savings_withdraw_requests--;
     });

     push_virtual_operation( fill_transfer_from_savings_operation( itr->from, itr->to, itr->amount, itr->request_id, to_string( itr->memo) ) );

     remove( *itr );
     itr = idx.begin();
  }
}


void database::expire_escrow_ratification()
{
   const auto& escrow_idx = get_index< escrow_index >().indices().get< by_ratification_deadline >();
   auto escrow_itr = escrow_idx.lower_bound( false );

   while( escrow_itr != escrow_idx.end() && !escrow_itr->is_approved() && escrow_itr->ratification_deadline <= head_block_time() )
   {
      const auto& old_escrow = *escrow_itr;
      ++escrow_itr;

      const auto& from_account = get_account( old_escrow.from );
      adjust_liquid_balance( from_account, old_escrow.balance);
      adjust_liquid_balance( from_account, old_escrow.pending_fee );

      remove( old_escrow );
   }
}

void database::update_median_liquidity()
{ try {
   if( (head_block_num() % MEDIAN_LIQUIDITY_INTERVAL_BLOCKS) != 0 )
      return;

   const auto& liq_idx = get_index< asset_liquidity_pool_index >().indices().get< by_asset_pair>();
   auto liq_itr = liq_idx.begin();

   size_t day_history_window = 1 + fc::days(1).to_seconds() / MEDIAN_LIQUIDITY_INTERVAL.to_seconds();
   size_t hour_history_window = 1 + fc::hours(1).to_seconds() / MEDIAN_LIQUIDITY_INTERVAL.to_seconds();

   while( liq_itr != liq_idx.end() )
   {
      const asset_liquidity_pool_object& pool = *liq_itr;
      vector<price> day; 
      vector<price> hour;
      day.reserve( day_history_window );
      hour.reserve( hour_history_window );

      bip::deque< price, allocator< price > > price_history = pool.price_history;

      modify( pool, [&]( asset_liquidity_pool_object& p )
      {
         p.price_history.push_back( pool.current_price() );
         if( p.price_history.size() > day_history_window )
         {
            price_history.pop_front();  // Maintain one day worth of price history
         }

         for( auto i : p.price_history )
         {
            day.push_back( i );
         }

         size_t offset = day.size()/2;

         std::nth_element( day.begin(), day.begin()+offset, day.end(),
         []( price a, price b )
         {
            return a < b;
         });

         p.day_median_price = day[ offset ];    // Set day median price to the median of all prices in the last day, at 10 min intervals

         auto hour_itr = p.price_history.rbegin();

         while( hour_itr != p.price_history.rend() && hour.size() < hour_history_window )
         {
            hour.push_back( *hour_itr );
            ++hour_itr;
         }

         size_t offset = hour.size()/2;

         std::nth_element( hour.begin(), hour.begin()+offset, hour.end(),
         []( price a, price b )
         {
            return a < b;
         });

         p.hour_median_price = hour[ offset ];   // Set hour median price to the median of all prices in the last hour, at 10 min intervals

      });

      ++liq_itr;
   } 
} FC_CAPTURE_AND_RETHROW() }


/**
 * Executes buyback orders to repurchase credit assets using
 * an asset's buyback pool of funds
 * up to the asset's buyback price, or face value.
 */         
void database::process_credit_buybacks()
{ try {
   if( (head_block_num() % CREDIT_BUYBACK_INTERVAL_BLOCKS) != 0 )
      return;

   const auto& credit_idx = get_index< asset_credit_data_index >().indices().get< by_symbol>();
   auto credit_itr = credit_idx.begin();

   while( credit_itr != credit_idx.end() )
   {
      const asset_credit_data_object& credit = *credit_itr;
      if( credit.buyback_pool.amount > 0 )
      {
         const asset_liquidity_pool_object& pool = get_liquidity_pool( credit.symbol_a, credit.symbol_b );
         price buyback_price = credit.buyback_price;
         price market_price = pool.base_hour_median_price( buyback_price.base.symbol );
         if( market_price > buyback_price )
         {
            pair<asset, asset> buyback = liquid_limit_exchange( credit.buyback_pool, buyback_price, pool, true, true );
            modify( credit, [&]( asset_credit_data_object& c )
            {
               c.adjust_pool( -buyback.first );    // buyback the credit asset from its liquidity pool, up to the buyback price, and deduct from the pool.
            });
            adjust_pending_supply( buyback.second );
         }
      }
      ++credit_itr;
   }
} FC_CAPTURE_AND_RETHROW() }

/**
 * Pays accrued interest to all balance holders of credit assets,
 * according to the fixed and variable components of the assets
 * interest options, and the current market price of the asset, 
 * relative to its target buyback face value price.
 * the interest rate increases when the the price of the credit asset falls,
 * and decreases, when it is above buyback price.
 */
void database::process_credit_interest()
{ try {
    if( (head_block_num() % CREDIT_INTERVAL_BLOCKS) != 0 )
      return;

   time_point now = head_block_time();
   const auto& credit_idx = get_index< asset_credit_data_index >().indices().get< by_symbol>();
   const auto& balance_idx = get_index< account_balance_index >().indices().get< by_symbol>();
   auto credit_itr = credit_idx.begin();

   while( credit_itr != credit_idx.end() )
   {
      const asset_credit_data_object& credit = *credit_itr;
      asset_symbol_type cs = credit.symbol;
      const asset_dynamic_data_object& dyn_data = get_dynamic_data( cs );
      price buyback = credit.buyback_price;
      price market = get_liquidity_pool( credit.symbol_a , credit.symbol_b ).base_hour_median_price( buyback.base.symbol );
      
      asset unit = asset( BLOCKCHAIN_PRECISION, buyback.quote.symbol );
      share_type range = credit.options.var_interest_range;
      share_type pr = PERCENT_100;
      share_type hpr = PERCENT_100 / 2;

      share_type mar = (market * unit).amount;
      share_type buy = (buy * unit).amount;

      share_type liqf = credit.options.liquid_fixed_interest_rate;
      share_type staf = credit.options.staked_fixed_interest_rate;
      share_type savf = credit.options.savings_fixed_interest_rate;
      share_type liqv = credit.options.liquid_variable_interest_rate;
      share_type stav = credit.options.staked_variable_interest_rate;
      share_type savv = credit.options.savings_variable_interest_rate;

      share_type var_factor = ( ( -hpr * std::min( pr, std::max( -pr, pr * ( mar-buy ) / ( ( ( buy*range ) / pr ) ) ) ) ) / pr ) + hpr;

      share_type liq_ir = liqv * var_factor + liqf;
      share_type sta_ir = stav * var_factor + staf;
      share_type sav_ir = savv * var_factor + savf;

      // Applies interest rates that scale with the current market price / buyback price ratio, within a specified boundary range.

      asset total_liquid_interest = asset(0, cs);
      asset total_staked_interest = asset(0, cs);
      asset total_savings_interest = asset(0, cs);

      auto balance_itr = balance_idx.lower_bound( cs );
      while( balance_itr != balance_idx.end() && balance_itr->symbol == cs )
      {
         const account_balance_object& balance = *balance_itr;

         asset liquid_interest = asset( ( ( ( balance.liquid_balance * liq_ir * ( now - balance.last_interest_time ).to_seconds() ) / fc::days(365).to_seconds() ) ) / pr , cs );
         asset staked_interest = asset( ( ( ( balance.staked_balance * sta_ir * ( now - balance.last_interest_time ).to_seconds() ) / fc::days(365).to_seconds() ) ) / pr , cs);
         asset savings_interest = asset( ( ( ( balance.savings_balance * sav_ir * ( now - balance.last_interest_time ).to_seconds() ) / fc::days(365).to_seconds() ) ) / pr , cs);

         total_liquid_interest += liquid_interest;
         total_staked_interest += staked_interest;
         total_savings_interest += savings_interest;

         modify( balance, [&]( account_balance_object& b )
         {
            b.adjust_liquid_balance( liquid_interest ); 
            b.adjust_staked_balance( staked_interest ); 
            b.adjust_savings_balance( savings_interest ); 
            b.last_interest_time = now;
         });
         
         ++balance_itr;
      }

      modify( dyn_data, [&]( asset_dynamic_data_object& d )
      {
         d.adjust_liquid_supply( total_liquid_interest ); 
         d.adjust_staked_supply( total_staked_interest ); 
         d.adjust_savings_supply( total_savings_interest ); 
      });

      ++credit_itr;
   }
} FC_CAPTURE_AND_RETHROW() }

/**
void database::update_median_feed() 
{ try {
   if( (head_block_num() % FEED_INTERVAL_BLOCKS) != 0 )
      return;

   auto now = head_block_time();
   const witness_schedule_object& wso = get_witness_schedule_object();
   vector<price> feeds; feeds.reserve( wso.num_scheduled_witnesses );
   for( int i = 0; i < wso.num_scheduled_witnesses; i++ )
   {
      const auto& wit = get_witness( wso.current_shuffled_producers[i] );
      if( now < wit.last_USD_exchange_update + MAX_FEED_AGE
         && !wit.USD_exchange_rate.is_null() )
      {
         feeds.push_back( wit.USD_exchange_rate );
      }
   }

   if( feeds.size() >= MIN_FEEDS )
   {
      std::sort( feeds.begin(), feeds.end() );
      auto median_feed = feeds[feeds.size()/2];

      modify( get_feed_history(), [&]( feed_history_object& fho )
      {
         fho.price_history.push_back( median_feed );

         size_t feed_history_window = FEED_HISTORY_WINDOW;

         if( fho.price_history.size() > feed_history_window )
            fho.price_history.pop_front();

         if( fho.price_history.size() )
         {
            std::deque< price > copy;
            for( auto i : fho.price_history )
            {
               copy.push_back( i );
            }

            std::sort( copy.begin(), copy.end() );
            fho.current_median_history = copy[copy.size()/2];
         }
      });
   }
} FC_CAPTURE_AND_RETHROW() }
*/


void database::clear_expired_delegations()
{
   auto now = head_block_time();
   const auto& delegations_by_exp = get_index< asset_delegation_expiration_index, by_expiration >();
   auto itr = delegations_by_exp.begin();
   while( itr != delegations_by_exp.end() && itr->expiration < now )
   {
      modify( get_account( itr->delegator ), [&]( account_object& a )
      {
         adjust_delegated_balance(a, itr->amount);
      });

      push_virtual_operation( return_asset_delegation_operation( itr->delegator, itr->amount ) );

      remove( *itr );
      itr = delegations_by_exp.begin();
   }
}

void database::adjust_liquid_balance( const account_object& a, const asset& delta )
{
   adjust_liquid_balance(a.name, delta);
}

void database::adjust_liquid_balance( const account_name_type& a, const asset& delta )
{ try {
   if( delta.amount == 0 )
   {
      return;
   }
   else if( a == NULL_ACCOUNT )
   {
      FC_ASSERT( delta.amount > 0,
         "Cannot reduce the balance of the Null Account. It has nothing." );
      if( delta.symbol == SYMBOL_COIN )
      {
         const dynamic_global_property_object& props = get_dynamic_global_properties();
         modify( props, [&]( dynamic_global_property_object& dgpo) 
         {
            dgpo.accumulated_network_revenue += delta;
         });
      }
      return;
   }

   const account_balance_object* account_balance_ptr = find_account_balance( a, delta.symbol );
   const asset_dynamic_data_object& asset_dyn_data = get_dynamic_data( delta.symbol );
   
   if( account_balance_ptr == nullptr )      // New balance object
   {
      FC_ASSERT( delta.amount > 0, "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}", 
         ("a", a )
         ("b", to_pretty_string( asset(0, delta.symbol)))
         ("r", to_pretty_string( -delta)));

      create<account_balance_object>( [&]( account_balance_object& abo) 
      {
         abo.owner = a;
         abo.symbol = delta.symbol;
         abo.liquid_balance = delta.amount;
         if( delta.symbol == SYMBOL_COIN )
         {
            abo.maintenance_flag = true;
         }
      });
      modify( asset_dyn_data, [&](asset_dynamic_data_object& addo) 
      {
         addo.adjust_liquid_supply(delta);
      });
   } 
   else 
   {
      if( delta.amount < 0 ) 
      {
         FC_ASSERT( account_balance_ptr->get_liquid_balance() >= -delta, 
            "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}",
                  ("a", a)
                  ("b", to_pretty_string( account_balance_ptr->get_liquid_balance() ))
                  ("r", to_pretty_string( -delta )));
      }
      modify( *account_balance_ptr, [&](account_balance_object& abo) 
      {
         abo.adjust_liquid_balance(delta);
      });

      modify( asset_dyn_data, [&](asset_dynamic_data_object& addo) 
      {
         addo.adjust_liquid_supply(delta);
      });
   }
} FC_CAPTURE_AND_RETHROW( (a)(delta) ) }

void database::adjust_staked_balance( const account_object& a, const asset& delta )
{
   adjust_staked_balance(a.name, delta);
}

void database::adjust_staked_balance( const account_name_type& a, const asset& delta )
{ try {
   if( delta.amount == 0 )
   {
      return;
   }
   else if( a == NULL_ACCOUNT )
   {
      FC_ASSERT( delta.amount > 0,
         "Cannot reduce the balance of the Null Account. It has nothing." );
      if( delta.symbol == SYMBOL_COIN )
      {
         const dynamic_global_property_object& props = get_dynamic_global_properties();
         modify( props, [&]( dynamic_global_property_object& dgpo) 
         {
            dgpo.accumulated_network_revenue += delta;
         });
      }
      return;
   }

   const account_balance_object* account_balance_ptr = find_account_balance( a, delta.symbol );
   const asset_dynamic_data_object& asset_dyn_data = get_dynamic_data( delta.symbol );
   
   if( account_balance_ptr == nullptr )      // New balance object
   {
      FC_ASSERT( delta.amount > 0, "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}", 
         ("a", a )
         ("b", to_pretty_string( asset(0, delta.symbol)))
         ("r", to_pretty_string( -delta)));

      create<account_balance_object>( [&]( account_balance_object& abo) 
      {
         abo.owner = a;
         abo.symbol = delta.symbol;
         abo.staked_balance = delta.amount;
         if( delta.symbol == SYMBOL_COIN )
         {
            abo.maintenance_flag = true;
         }
      });
      modify( asset_dyn_data, [&](asset_dynamic_data_object& addo) 
      {
         addo.adjust_staked_supply(delta);
      });
   } 
   else 
   {
      if( delta.amount < 0 ) 
      {
         FC_ASSERT( account_balance_ptr->get_staked_balance() >= -delta, 
            "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}",
                  ("a", a)
                  ("b", to_pretty_string( account_balance_ptr->get_staked_balance() ))
                  ("r", to_pretty_string( -delta )));
      }
      modify( *account_balance_ptr, [&](account_balance_object& abo) 
      {
         abo.adjust_staked_balance(delta);
      });

      modify( asset_dyn_data, [&](asset_dynamic_data_object& addo) 
      {
         addo.adjust_staked_supply(delta);
      });
   }
} FC_CAPTURE_AND_RETHROW( (a)(delta) ) }

void database::adjust_savings_balance( const account_object& a, const asset& delta )
{
   adjust_savings_balance(a.name, delta);
}

void database::adjust_savings_balance( const account_name_type& a, const asset& delta )
{ try {
   if( delta.amount == 0 )
   {
      return;
   }
   else if( a == NULL_ACCOUNT )
   {
      FC_ASSERT( delta.amount > 0,
         "Cannot reduce the balance of the Null Account. It has nothing." );
      if( delta.symbol == SYMBOL_COIN )
      {
         const dynamic_global_property_object& props = get_dynamic_global_properties();
         modify( props, [&]( dynamic_global_property_object& dgpo) 
         {
            dgpo.accumulated_network_revenue += delta;
         });
      }
      return;
   }

   const account_balance_object* account_balance_ptr = find_account_balance( a, delta.symbol );
   const asset_dynamic_data_object& asset_dyn_data = get_dynamic_data( delta.symbol );
   
   if( account_balance_ptr == nullptr )      // New balance object
   {
      FC_ASSERT( delta.amount > 0, "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}", 
         ("a", a )
         ("b", to_pretty_string( asset(0, delta.symbol)))
         ("r", to_pretty_string( -delta)));

      create<account_balance_object>( [&]( account_balance_object& abo) 
      {
         abo.owner = a;
         abo.symbol = delta.symbol;
         abo.savings_balance = delta.amount;
         if( delta.symbol == SYMBOL_COIN )
         {
            abo.maintenance_flag = true;
         }
      });
      modify( asset_dyn_data, [&](asset_dynamic_data_object& addo) 
      {
         addo.adjust_savings_supply(delta);
      });
   } 
   else 
   {
      if( delta.amount < 0 ) 
      {
         FC_ASSERT( account_balance_ptr->get_savings_balance() >= -delta, 
            "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}",
                  ("a", a)
                  ("b", to_pretty_string( account_balance_ptr->get_savings_balance() ))
                  ("r", to_pretty_string( -delta )));
      }
      modify( *account_balance_ptr, [&](account_balance_object& abo) 
      {
         abo.adjust_savings_balance(delta);
      });

      modify( asset_dyn_data, [&](asset_dynamic_data_object& addo) 
      {
         addo.adjust_savings_supply(delta);
      });
   }
} FC_CAPTURE_AND_RETHROW( (a)(delta) ) }

void database::adjust_reward_balance( const account_object& a, const asset& delta )
{
   adjust_reward_balance(a.name, delta);
}

void database::adjust_reward_balance( const account_name_type& a, const asset& delta )
{ try {
   if( delta.amount == 0 )
   {
      return;
   }
   else if( a == NULL_ACCOUNT )
   {
      FC_ASSERT( delta.amount > 0,
         "Cannot reduce the balance of the Null Account. It has nothing." );
      if( delta.symbol == SYMBOL_COIN )
      {
         const dynamic_global_property_object& props = get_dynamic_global_properties();
         modify( props, [&]( dynamic_global_property_object& dgpo) 
         {
            dgpo.accumulated_network_revenue += delta;
         });
      }
      return;
   }

   const account_balance_object* account_balance_ptr = find_account_balance( a, delta.symbol );
   const asset_dynamic_data_object& asset_dyn_data = get_dynamic_data( delta.symbol );
   
   if( account_balance_ptr == nullptr )      // New balance object
   {
      FC_ASSERT( delta.amount > 0, "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}", 
         ("a", a )
         ("b", to_pretty_string( asset(0, delta.symbol)))
         ("r", to_pretty_string( -delta)));

      create<account_balance_object>( [&]( account_balance_object& abo) 
      {
         abo.owner = a;
         abo.symbol = delta.symbol;
         abo.reward_balance = delta.amount;
         if( delta.symbol == SYMBOL_COIN )
         {
            abo.maintenance_flag = true;
         }
      });
      modify( asset_dyn_data, [&](asset_dynamic_data_object& addo) 
      {
         addo.adjust_reward_supply(delta);
      });
   } 
   else 
   {
      if( delta.amount < 0 ) 
      {
         FC_ASSERT( account_balance_ptr->get_reward_balance() >= -delta, 
            "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}",
                  ("a", a)
                  ("b", to_pretty_string( account_balance_ptr->get_reward_balance() ))
                  ("r", to_pretty_string( -delta )));
      }
      modify( *account_balance_ptr, [&](account_balance_object& abo) 
      {
         abo.adjust_reward_balance(delta);
      });

      modify( asset_dyn_data, [&](asset_dynamic_data_object& addo) 
      {
         addo.adjust_reward_supply(delta);
      });
   }
} FC_CAPTURE_AND_RETHROW( (a)(delta) ) }

void database::adjust_delegated_balance( const account_object& a, const asset& delta )
{
   adjust_delegated_balance(a.name, delta);
}

void database::adjust_delegated_balance( const account_name_type& a, const asset& delta )
{ try {
   if( delta.amount == 0 )
   {
      return;
   }
   else if( a == NULL_ACCOUNT )
   {
      FC_ASSERT( delta.amount > 0,
         "Cannot reduce the balance of the Null Account. It has nothing." );
      if( delta.symbol == SYMBOL_COIN )
      {
         const dynamic_global_property_object& props = get_dynamic_global_properties();
         modify( props, [&]( dynamic_global_property_object& dgpo) 
         {
            dgpo.accumulated_network_revenue += delta;
         });
      }
      return;
   }

   const account_balance_object* account_balance_ptr = find_account_balance( a, delta.symbol );
   const asset_dynamic_data_object& asset_dyn_data = get_dynamic_data( delta.symbol );
   
   if( account_balance_ptr == nullptr )      // New balance object
   {
      FC_ASSERT( delta.amount > 0, "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}", 
         ("a", a )
         ("b", to_pretty_string( asset(0, delta.symbol)))
         ("r", to_pretty_string( -delta)));

      create<account_balance_object>( [&]( account_balance_object& abo) 
      {
         abo.owner = a;
         abo.symbol = delta.symbol;
         abo.delegated_balance = delta.amount;
         if( delta.symbol == SYMBOL_COIN )
         {
            abo.maintenance_flag = true;
         }
      });
      modify( asset_dyn_data, [&](asset_dynamic_data_object& addo) 
      {
         addo.adjust_delegated_supply(delta);
      });
   } 
   else 
   {
      if( delta.amount < 0 ) 
      {
         FC_ASSERT( account_balance_ptr->get_delegated_balance() >= -delta, 
            "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}",
                  ("a", a)
                  ("b", to_pretty_string( account_balance_ptr->get_delegated_balance() ))
                  ("r", to_pretty_string( -delta )));
      }
      modify( *account_balance_ptr, [&](account_balance_object& abo) 
      {
         abo.adjust_delegated_balance(delta);
      });

      modify( asset_dyn_data, [&](asset_dynamic_data_object& addo) 
      {
         addo.adjust_delegated_supply(delta);
      });
   }
} FC_CAPTURE_AND_RETHROW( (a)(delta) ) }

void database::adjust_receiving_balance( const account_object& a, const asset& delta )
{
   adjust_receiving_balance(a.name, delta);
}

void database::adjust_receiving_balance( const account_name_type& a, const asset& delta )
{ try {
   if( delta.amount == 0 )
   {
      return;
   }
   else if( a == NULL_ACCOUNT )
   {
      FC_ASSERT( delta.amount > 0,
         "Cannot reduce the balance of the Null Account. It has nothing." );
      if( delta.symbol == SYMBOL_COIN )
      {
         const dynamic_global_property_object& props = get_dynamic_global_properties();
         modify( props, [&]( dynamic_global_property_object& dgpo) 
         {
            dgpo.accumulated_network_revenue += delta;
         });
      }
      return;
   }

   const account_balance_object* account_balance_ptr = find_account_balance( a, delta.symbol );
   const asset_dynamic_data_object& asset_dyn_data = get_dynamic_data( delta.symbol );
   
   if( account_balance_ptr == nullptr )      // New balance object
   {
      FC_ASSERT( delta.amount > 0, "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}", 
         ("a", a )
         ("b", to_pretty_string( asset(0, delta.symbol)))
         ("r", to_pretty_string( -delta)));

      create<account_balance_object>( [&]( account_balance_object& abo) 
      {
         abo.owner = a;
         abo.symbol = delta.symbol;
         abo.receiving_balance = delta.amount;
         if( delta.symbol == SYMBOL_COIN )
         {
            abo.maintenance_flag = true;
         }
      });
      modify( asset_dyn_data, [&](asset_dynamic_data_object& addo) 
      {
         addo.adjust_receiving_supply(delta);
      });
   } 
   else 
   {
      if( delta.amount < 0 ) 
      {
         FC_ASSERT( account_balance_ptr->get_receiving_balance() >= -delta, 
            "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}",
                  ("a", a)
                  ("b", to_pretty_string( account_balance_ptr->get_receiving_balance() ))
                  ("r", to_pretty_string( -delta )));
      }
      modify( *account_balance_ptr, [&](account_balance_object& abo) 
      {
         abo.adjust_receiving_balance(delta);
      });

      modify( asset_dyn_data, [&](asset_dynamic_data_object& addo) 
      {
         addo.adjust_receiving_supply(delta);
      });
   }
} FC_CAPTURE_AND_RETHROW( (a)(delta) ) }


void database::adjust_pending_supply( const asset& delta )
{ try {
   if( delta.amount == 0 )
   {
      return;
   }

   const asset_dynamic_data_object& asset_dyn_data = get_dynamic_data( delta.symbol );
   if( delta.amount < 0 ) 
   {
      FC_ASSERT( asset_dyn_data.get_pending_supply() >= -delta, 
         "Insufficient Pending supply: ${a}'s balance of ${b} is less than required ${r}",
               ("a", delta.symbol)
               ("b", to_pretty_string( asset_dyn_data.get_pending_supply() ))
               ("r", to_pretty_string( -delta )));
   }
   
   modify( asset_dyn_data, [&](asset_dynamic_data_object& addo) 
   {
      addo.adjust_pending_supply( delta );
   });
   
} FC_CAPTURE_AND_RETHROW( (delta) ) }


asset database::get_liquid_balance( const account_object& a, const asset_symbol_type& symbol )const
{
   return get_liquid_balance(a.name, symbol);
}

asset database::get_liquid_balance( const account_name_type& a, const asset_symbol_type& symbol)const
{ try {
   const account_balance_object* abo_ptr = find_account_balance(a, symbol);
   if( abo_ptr == nullptr )
   {
      return asset(0, symbol);
   }
   else
   {
      return abo_ptr->get_liquid_balance();
   }
} FC_CAPTURE_AND_RETHROW( (a)(symbol) ) }

asset database::get_staked_balance( const account_object& a, const asset_symbol_type& symbol )const
{
   return get_staked_balance(a.name, symbol);
}

asset database::get_staked_balance( const account_name_type& a, const asset_symbol_type& symbol)const
{ try {
   const account_balance_object* abo_ptr = find_account_balance(a, symbol);
   if( abo_ptr == nullptr )
   {
      return asset(0, symbol);
   }
   else
   {
      return abo_ptr->get_staked_balance();
   }
} FC_CAPTURE_AND_RETHROW( (a)(symbol) ) }

asset database::get_reward_balance( const account_object& a, const asset_symbol_type& symbol )const
{
   return get_reward_balance(a.name, symbol);
}

asset database::get_reward_balance( const account_name_type& a, const asset_symbol_type& symbol)const
{ try {
   const account_balance_object* abo_ptr = find_account_balance(a, symbol);
   if( abo_ptr == nullptr )
   {
      return asset(0, symbol);
   }
   else
   {
      return abo_ptr->get_reward_balance();
   }
} FC_CAPTURE_AND_RETHROW( (a)(symbol) ) }

asset database::get_savings_balance( const account_object& a, const asset_symbol_type& symbol )const
{
   return get_savings_balance(a.name, symbol);
}

asset database::get_savings_balance( const account_name_type& a, const asset_symbol_type& symbol)const
{ try {
   const account_balance_object* abo_ptr = find_account_balance(a, symbol);
   if( abo_ptr == nullptr )
   {
      return asset(0, symbol);
   }
   else
   {
      return abo_ptr->get_savings_balance();
   }
} FC_CAPTURE_AND_RETHROW( (a)(symbol) ) }

asset database::get_delegated_balance( const account_object& a, const asset_symbol_type& symbol )const
{
   return get_delegated_balance(a.name, symbol);
}

asset database::get_delegated_balance( const account_name_type& a, const asset_symbol_type& symbol)const
{ try {
   const account_balance_object* abo_ptr = find_account_balance(a, symbol);
   if( abo_ptr == nullptr )
   {
      return asset(0, symbol);
   }
   else
   {
      return abo_ptr->get_delegated_balance();
   }
} FC_CAPTURE_AND_RETHROW( (a)(symbol) ) }

asset database::get_receiving_balance( const account_object& a, const asset_symbol_type& symbol )const
{
   return get_receiving_balance(a.name, symbol);
}

asset database::get_receiving_balance( const account_name_type& a, const asset_symbol_type& symbol)const
{ try {
   const account_balance_object* abo_ptr = find_account_balance(a, symbol);
   if( abo_ptr == nullptr )
   {
      return asset(0, symbol);
   }
   else
   {
      return abo_ptr->get_receiving_balance();
   }
} FC_CAPTURE_AND_RETHROW( (a)(symbol) ) }

share_type database::get_voting_power( const account_object& a )const
{
   return get_voting_power(a.name);
}

share_type database::get_voting_power( const account_name_type& a )const
{
   const account_balance_object* coin_ptr = find_account_balance(a, SYMBOL_COIN);
   const account_balance_object* equity_ptr = find_account_balance(a, SYMBOL_EQUITY);
   price equity_coin_price = get_liquidity_pool( SYMBOL_COIN, SYMBOL_EQUITY).hour_median_price;
   share_type voting_power = 0;
   if( coin_ptr != nullptr )
   {
      voting_power += coin_ptr->get_voting_power().amount;
   }
   if( equity_ptr != nullptr )
   {
      voting_power += (equity_ptr->get_voting_power()*equity_coin_price).amount;
   }
   return voting_power;
}

share_type database::get_voting_power( const account_object& a, const price& equity_coin_price )const
{
   return get_voting_power( a.name, equity_coin_price );
}

share_type database::get_voting_power( const account_name_type& a, const price& equity_coin_price )const
{
   const account_balance_object* coin_ptr = find_account_balance(a, SYMBOL_COIN);
   const account_balance_object* equity_ptr = find_account_balance(a, SYMBOL_EQUITY);
   share_type voting_power = 0;
   if( coin_ptr != nullptr )
   {
      voting_power += coin_ptr->get_voting_power().amount;
   }
   if( equity_ptr != nullptr )
   {
      voting_power += (equity_ptr->get_voting_power()*equity_coin_price).amount;
   }
   return voting_power;
}

share_type database::get_proxied_voting_power( const account_object& a, const price& equity_price )const
{ try {
   if( a.proxied.size() == 0 )
   {
      return 0;
   }
   share_type voting_power = 0;
   for( auto name : a.proxied )
   {
      voting_power += get_voting_power( name, equity_price );
      voting_power += get_proxied_voting_power( name, equity_price );    // Recursively finds voting power of proxies
   }
   return voting_power;

} FC_CAPTURE_AND_RETHROW() }


share_type database::get_proxied_voting_power( const account_name_type& a, const price& equity_price )const
{ try {
   return get_proxied_voting_power( get_account(a), equity_price);
} FC_CAPTURE_AND_RETHROW() }


share_type database::get_equity_voting_power( const account_object& a, const account_business_object& b )const
{ try {
   return get_equity_voting_power( a.name, b);
} FC_CAPTURE_AND_RETHROW() }


share_type database::get_equity_voting_power( const account_name_type& a, const account_business_object& b )const
{ try {
   share_type voting_power = 0;
   for( auto symbol : b.equity_assets )
   {
      const asset_equity_data_object& equity = get_equity_data( symbol );
      const account_balance_object* abo_ptr = find_account_balance( a, symbol );
      if( abo_ptr != nullptr )
      {
         voting_power += abo_ptr->get_liquid_balance() * equity.options.liquid_voting_rights;
         voting_power += abo_ptr->get_voting_power() * equity.options.staked_voting_rights;
         voting_power += abo_ptr->get_savings_balance() * equity.options.savings_voting_rights;
      }
   }
   return voting_power;
} FC_CAPTURE_AND_RETHROW() }


string database::to_pretty_string( const asset& a )const
{
   return a.amount_to_pretty_string(a);
}


void database::update_expired_feeds()
{
   const auto head_time = head_block_time();
   const auto next_maint_time = get_dynamic_global_properties().next_maintenance_time;

   const auto& idx = get_index<asset_bitasset_data_index>().indices().get<by_feed_expiration>();
   auto itr = idx.begin();
   while( itr != idx.end() && itr->feed_is_expired( head_time ) ) // update feeds, check margin calls for each asset whose feed is expired
   {
      const asset_bitasset_data_object& bitasset = *itr;
      ++itr; 
      bool update_cer = false; 
      const asset_object* asset_ptr = nullptr;
      
      auto old_median_feed = bitasset.current_feed;
      modify( bitasset, [head_time,next_maint_time,&update_cer]( asset_bitasset_data_object& abdo )
      {
         abdo.update_median_feeds( head_time, next_maint_time );
         if( abdo.need_to_update_cer() )
         {
            update_cer = true;
            abdo.asset_cer_updated = false;
            abdo.feed_cer_updated = false;
         }
      });

      if( !bitasset.current_feed.settlement_price.is_null() && !( bitasset.current_feed == old_median_feed ) ) // `==` check is safe here
      {
         asset_ptr = find_asset(bitasset.symbol);
         check_call_orders( *asset_ptr, true, false );
      }
      
      if( update_cer ) // update CER
      {
         if( !asset_ptr )
            asset_ptr = find_asset(bitasset.symbol);
         if( asset_ptr->options.core_exchange_rate != bitasset.current_feed.core_exchange_rate )
         {
            modify( *asset_ptr, [&]( asset_object& ao )
            {
               ao.options.core_exchange_rate = bitasset.current_feed.core_exchange_rate;
            });
         }
      }
   } 
}

void database::update_core_exchange_rates()
{
   const auto& idx = get_index<asset_bitasset_data_index>().indices().get<by_cer_update>();
   if( idx.begin() != idx.end() )
   {
      for( auto itr = idx.rbegin(); itr->need_to_update_cer(); itr = idx.rbegin() )
      {
         const asset_bitasset_data_object& bitasset = *itr;
         const asset_object& asset = get_asset ( bitasset.symbol );
         if( asset.options.core_exchange_rate != bitasset.current_feed.core_exchange_rate )
         {
            modify( asset, [&bitasset]( asset_object& ao )
            {
               ao.options.core_exchange_rate = bitasset.current_feed.core_exchange_rate;
            });
         }
         modify( bitasset, []( asset_bitasset_data_object& abdo )
         {
            abdo.asset_cer_updated = false;
            abdo.feed_cer_updated = false;
         });
      }
   }
}

void database::update_maintenance_flag( bool new_maintenance_flag )
{
   const dynamic_global_property_object& props = get_dynamic_global_properties();
   modify(props, [&]( dynamic_global_property_object& dpo )
   {
      auto maintenance_flag = dynamic_global_property_object::maintenance_flag;
      dpo.dynamic_flags =
           (dpo.dynamic_flags & ~maintenance_flag)
         | (new_maintenance_flag ? maintenance_flag : 0);
   } );
   return;
}

} } //node::chain