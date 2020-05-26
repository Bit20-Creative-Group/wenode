#include <boost/test/unit_test.hpp>
#include <node/protocol/exceptions.hpp>
#include <node/chain/database.hpp>
#include <node/chain/database_exceptions.hpp>
#include <node/chain/util/reward.hpp>
#include <fc/crypto/digest.hpp>
#include "../common/database_fixture.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace node;
using namespace node::chain;
using namespace node::protocol;
using std::string;

BOOST_FIXTURE_TEST_SUITE( pool_operation_tests, clean_database_fixture );



   //====================//
   // === Pool Tests === //
   //====================//



BOOST_AUTO_TEST_CASE( liquidity_pool_operation_sequence_test )
{
   try
   {
      BOOST_TEST_MESSAGE( "├── Testing: LIQUIDITY POOL OPERATION SEQUENCE" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Creation of a new liquidity pool" );

      fund( INIT_ACCOUNT, asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );

      ACTORS( (alice)(bob)(candice)(dan)(elon)(fred)(george)(haz) );

      fund( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund_stake( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund_stake( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );

      fund( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund_stake( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund_stake( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      
      fund( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund_stake( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund_stake( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );

      fund( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund_stake( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund_stake( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      
      signed_transaction tx;

      asset_create_operation asset_create;

      asset_create.signatory = "alice";
      asset_create.issuer = "alice";
      asset_create.symbol = "ALICECOIN";
      asset_create.asset_type = "standard";
      asset_create.coin_liquidity = asset( 100 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      asset_create.usd_liquidity = asset( 100 * BLOCKCHAIN_PRECISION, SYMBOL_USD );
      asset_create.credit_liquidity = asset( 100 * BLOCKCHAIN_PRECISION, "ALICECOIN" );
      asset_create.options.display_symbol = "Alice Coin";
      asset_create.options.details = "Details";
      asset_create.options.json = "{ \"valid\": true }";
      asset_create.options.url = "https://www.url.com";
      asset_create.validate();

      tx.operations.push_back( asset_create );
      tx.set_expiration( now() + fc::seconds( MAX_TIME_UNTIL_EXPIRATION ) );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      asset_create.signatory = "bob";
      asset_create.issuer = "bob";
      asset_create.symbol = "BOBCOIN";
      asset_create.asset_type = "standard";
      asset_create.coin_liquidity = asset( 100 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      asset_create.usd_liquidity = asset( 100 * BLOCKCHAIN_PRECISION, SYMBOL_USD );
      asset_create.credit_liquidity = asset( 100 * BLOCKCHAIN_PRECISION, "BOBCOIN" );
      asset_create.options.display_symbol = "Bob Coin";
      asset_create.options.details = "Details";
      asset_create.options.json = "{ \"valid\": true }";
      asset_create.options.url = "https://www.url.com";
      asset_create.validate();

      tx.operations.push_back( asset_create );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      asset_issue_operation issue;

      issue.signatory = "alice";
      issue.issuer = "alice";
      issue.asset_to_issue = asset( 10000 * BLOCKCHAIN_PRECISION, "ALICECOIN" );
      issue.issue_to_account = "alice";
      issue.memo = "Hello";
      issue.validate();

      tx.operations.push_back( issue );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      issue.signatory = "bob";
      issue.issuer = "bob";
      issue.asset_to_issue = asset( 10000 * BLOCKCHAIN_PRECISION, "BOBCOIN" );
      issue.issue_to_account = "alice";
      issue.memo = "Hello";

      tx.operations.push_back( issue );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      liquidity_pool_create_operation pool_create;

      pool_create.signatory = "alice";
      pool_create.account = "alice";
      pool_create.first_amount = asset( 1000 * BLOCKCHAIN_PRECISION, "ALICECOIN" );
      pool_create.second_amount = asset( 1000 * BLOCKCHAIN_PRECISION, "BOBCOIN" );
      pool_create.validate();

      tx.operations.push_back( pool_create );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      const asset_liquidity_pool_object& pool = db.get_liquidity_pool( "ALICECOIN", "BOBCOIN" );
      asset_symbol_type liquidity_asset_symbol = string( LIQUIDITY_ASSET_PREFIX )+"ALICECOIN.BOBCOIN";
      share_type max = std::max( pool_create.first_amount.amount, pool_create.second_amount.amount );

      BOOST_REQUIRE( pool.issuer == pool_create.account );
      BOOST_REQUIRE( pool.symbol_a == pool_create.first_amount.symbol );
      BOOST_REQUIRE( pool.symbol_b == pool_create.second_amount.symbol );
      BOOST_REQUIRE( pool.balance_a == pool_create.first_amount );
      BOOST_REQUIRE( pool.balance_b == pool_create.second_amount );
      BOOST_REQUIRE( pool.symbol_liquid == liquidity_asset_symbol );
      BOOST_REQUIRE( pool.balance_liquid == asset( max, liquidity_asset_symbol ) );

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Creation of a new liquidity pool" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when making a liquidity pool using a liquidity pool asset" );

      pool_create.signatory = "alice";
      pool_create.account = "alice";
      pool_create.first_amount = asset( 500 * BLOCKCHAIN_PRECISION, liquidity_asset_symbol );
      pool_create.second_amount = asset( 500 * BLOCKCHAIN_PRECISION, "BOBCOIN" );

      tx.operations.push_back( pool_create );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when making a liquidity pool using a liquidity pool asset" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Exchange with the liquidity pool" );

      liquidity_pool_exchange_operation exchange;

      exchange.signatory = "alice";
      exchange.account = "alice";
      exchange.amount = asset( 10 * BLOCKCHAIN_PRECISION, "ALICECOIN" );
      exchange.receive_asset = "BOBCOIN";
      exchange.interface = INIT_ACCOUNT;
      exchange.acquire = false;
      exchange.validate();

      tx.operations.push_back( exchange );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Exchange with the liquidity pool" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Exchange acquisition from the liquidity pool" );

      exchange.acquire = true;
      
      tx.operations.push_back( exchange );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Exchange acquisition from the liquidity pool" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: failure when account does not have required funds" );

      exchange.amount = asset( 20000 * BLOCKCHAIN_PRECISION, "ALICECOIN" );
      exchange.acquire = false;
      
      tx.operations.push_back( exchange );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: failure when account does not have required funds" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: failure when acquiring more than available from pool" );

      exchange.amount = asset( 20000 * BLOCKCHAIN_PRECISION, "ALICECOIN" );
      exchange.acquire = true;
      
      tx.operations.push_back( exchange );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: failure when acquiring more than available from pool" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: failure when exchange amount is 0" );

      exchange.amount = asset( 0, "ALICECOIN" );
      exchange.acquire = false;
      
      tx.operations.push_back( exchange );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: failure when exchange amount is 0" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Add funds to the liquidity pool" );

      liquidity_pool_fund_operation fund;

      fund.signatory = "alice";
      fund.account = "alice";
      fund.amount = asset( 1000 * BLOCKCHAIN_PRECISION, "ALICECOIN" );
      fund.pair_asset = liquidity_asset_symbol;
      fund.validate();

      tx.operations.push_back( fund );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Add funds to the liquidity pool" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure adding funds to the liquidity pool without sufficient funds" );

      fund.amount = asset( 1000000, "ALICECOIN" );

      tx.operations.push_back( fund );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure adding funds to the liquidity pool without sufficient funds" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure adding funds to the liquidity pool when amount is 0" );

      fund.amount = asset( 0, "ALICECOIN" );

      tx.operations.push_back( fund );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure adding funds to the liquidity pool when amount is 0" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Withdraw funds from the liquidity pool" );

      liquidity_pool_withdraw_operation withdraw;

      withdraw.signatory = "alice";
      withdraw.account = "alice";
      withdraw.amount = asset( 500 * BLOCKCHAIN_PRECISION, liquidity_asset_symbol );
      withdraw.receive_asset = "ALICECOIN";
      withdraw.validate();

      tx.operations.push_back( withdraw );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Withdraw funds from the liquidity pool" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when withdrawing too many funds from the liquidity pool" );

      withdraw.amount = asset( 1000000 * BLOCKCHAIN_PRECISION, liquidity_asset_symbol );

      tx.operations.push_back( withdraw );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when withdrawing too many funds from the liquidity pool" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when withdrawing 0 amount from the liquidity pool" );

      withdraw.amount = asset( 0, liquidity_asset_symbol );

      tx.operations.push_back( withdraw );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when withdrawing 0 amount from the liquidity pool" );

      BOOST_TEST_MESSAGE( "├── Testing: LIQUIDITY POOL OPERATION SEQUENCE" );
   }
   FC_LOG_AND_RETHROW()
}


BOOST_AUTO_TEST_CASE( credit_pool_operation_sequence_test )
{
   try
   {
      BOOST_TEST_MESSAGE( "├── Testing: CREDIT POOL OPERATION SEQUENCE" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Create credit collateral position" );

      fund( INIT_ACCOUNT, asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );

      ACTORS( (alice)(bob)(candice)(dan)(elon) );

      fund( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund_stake( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund_stake( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
      fund_stake( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
      producer_create( "alice", alice_private_owner_key, alice_public_owner_key );

      fund( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund_stake( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund_stake( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
      fund_stake( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
      producer_create( "bob", bob_private_owner_key, bob_public_owner_key );
      
      fund( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund_stake( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund_stake( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
      fund_stake( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
      producer_create( "candice", candice_private_owner_key, candice_public_owner_key );

      fund( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund_stake( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund_stake( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
      fund_stake( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
      producer_create( "dan", dan_private_owner_key, dan_public_owner_key );

      fund( "elon", asset( 1000000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund_stake( "elon", asset( 1000000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund( "elon", asset( 1000000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund_stake( "elon", asset( 1000000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund( "elon", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
      fund_stake( "elon", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_CREDIT ) );
      producer_create( "elon", elon_private_owner_key, dan_public_owner_key );
      
      signed_transaction tx;

      liquidity_pool_fund_operation fund;

      fund.signatory = "elon";
      fund.account = "elon";
      fund.amount = asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      fund.pair_asset = SYMBOL_USD;
      fund.validate();

      tx.operations.push_back( fund );
      tx.set_expiration( now() + fc::seconds( MAX_TIME_UNTIL_EXPIRATION ) );
      tx.sign( elon_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      fund.amount = asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD );

      tx.operations.push_back( fund );
      tx.sign( elon_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      fund.amount = asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      fund.pair_asset = SYMBOL_CREDIT;

      tx.operations.push_back( fund );
      tx.sign( elon_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      fund.amount = asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_CREDIT );

      tx.operations.push_back( fund );
      tx.sign( elon_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      asset_publish_feed_operation feed;

      feed.signatory = "alice";
      feed.publisher = "alice";
      feed.symbol = SYMBOL_USD;
      feed.feed.settlement_price = price( asset( BLOCKCHAIN_PRECISION, SYMBOL_USD ), asset( BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      feed.validate();

      tx.operations.push_back( feed );
      tx.set_expiration( now() + fc::seconds( MAX_TIME_UNTIL_EXPIRATION ) );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      feed.signatory = "bob";
      feed.publisher = "bob";

      tx.operations.push_back( feed );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      feed.signatory = "candice";
      feed.publisher = "candice";

      tx.operations.push_back( feed );
      tx.sign( candice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      feed.signatory = "dan";
      feed.publisher = "dan";

      tx.operations.push_back( feed );
      tx.sign( dan_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      credit_pool_collateral_operation collateral;

      collateral.signatory = "alice";
      collateral.account = "alice";
      collateral.amount = asset( 10000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      collateral.validate();

      tx.operations.push_back( collateral );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      const credit_collateral_object& alice_collateral = db.get_collateral( "alice", SYMBOL_COIN );

      BOOST_REQUIRE( alice_collateral.owner == collateral.account );
      BOOST_REQUIRE( alice_collateral.symbol == collateral.amount.symbol );
      BOOST_REQUIRE( alice_collateral.collateral == collateral.amount );
      BOOST_REQUIRE( alice_collateral.created == now() );
      BOOST_REQUIRE( alice_collateral.last_updated == now() );

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Create credit collateral position" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when making a credit collateral position without sufficient balance" );

      collateral.amount = asset( 1000000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );

      tx.operations.push_back( collateral );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when making a credit collateral position without sufficient balance" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when amount of collateral is not changed" );

      collateral.amount = asset( 10000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );

      tx.operations.push_back( collateral );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      const auto& col_idx = db.get_index< credit_collateral_index >().indices().get< by_owner_symbol >();
      auto col_itr = col_idx.find( std::make_tuple( "alice", SYMBOL_COIN ) );

      BOOST_REQUIRE( col_itr == col_idx.end() );

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when amount of collateral is not changed" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Removing Collateral position" );

      collateral.amount = asset( 0, SYMBOL_COIN );

      tx.operations.push_back( collateral );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      col_itr = col_idx.find( std::make_tuple( "alice", SYMBOL_COIN ) );

      BOOST_REQUIRE( col_itr == col_idx.end() );

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Removing Collateral position" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Lending funds to credit pool" );

      credit_pool_lend_operation lend;

      lend.signatory = "bob";
      lend.account = "bob";
      lend.amount = asset( 50000 * BLOCKCHAIN_PRECISION, SYMBOL_USD );

      tx.operations.push_back( lend );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      const asset_credit_pool_object& usd_credit_pool = db.get_credit_pool( SYMBOL_USD, false );

      BOOST_REQUIRE( usd_credit_pool.base_symbol == lend.amount.symbol );
      BOOST_REQUIRE( usd_credit_pool.base_balance == lend.amount );
      BOOST_REQUIRE( usd_credit_pool.borrowed_balance.amount == 0 );
      asset_symbol_type usd_credit_symbol = usd_credit_pool.credit_symbol;
   
      tx.operations.clear();
      tx.signatures.clear();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Lending funds to credit pool" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when insufficient balance to lend" );

      lend.amount = asset( 1000000 * BLOCKCHAIN_PRECISION, SYMBOL_USD );

      tx.operations.push_back( collateral );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when insufficient balance to lend" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when lending 0" );

      lend.amount = asset( 0, SYMBOL_USD );

      tx.operations.push_back( collateral );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when lending 0" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when lending credit pool asset" );

      lend.amount = asset( 5000, usd_credit_symbol );

      tx.operations.push_back( collateral );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when lending credit pool asset" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Creation of Borrowing order from credit pool" );

      collateral.signatory = "alice";
      collateral.account = "alice";
      collateral.amount = asset( 20000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );

      tx.operations.push_back( collateral );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      credit_pool_borrow_operation borrow;

      borrow.signatory = "alice";
      borrow.account = "alice";
      borrow.amount = asset( 5000 * BLOCKCHAIN_PRECISION, SYMBOL_USD );
      borrow.collateral = asset( 10000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      borrow.loan_id = "7d8f6c1a-0409-416f-9e07-f60c46381a92";
      borrow.validate();

      tx.operations.push_back( borrow );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_REQUIRE( usd_credit_pool.base_balance == lend.amount - borrow.amount );
      BOOST_REQUIRE( usd_credit_pool.borrowed_balance == borrow.amount );

      const credit_loan_object& alice_loan = db.get_loan( "alice", string( "7d8f6c1a-0409-416f-9e07-f60c46381a92" ) );

      BOOST_REQUIRE( alice_loan.debt == borrow.amount );
      BOOST_REQUIRE( alice_loan.collateral == borrow.collateral );

      BOOST_REQUIRE( alice_collateral.collateral == collateral.amount - borrow.collateral );
      BOOST_REQUIRE( alice_collateral.last_updated == now() );

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Creation of Borrowing order from credit pool" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Updating Borrowing order to increase loan debt" );

      borrow.amount = asset( 7500 * BLOCKCHAIN_PRECISION, SYMBOL_USD );
      
      tx.operations.push_back( borrow );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_REQUIRE( usd_credit_pool.base_balance == lend.amount - borrow.amount );
      BOOST_REQUIRE( usd_credit_pool.borrowed_balance == borrow.amount );

      BOOST_REQUIRE( alice_loan.debt == borrow.amount );
      BOOST_REQUIRE( alice_loan.collateral == borrow.collateral );

      BOOST_REQUIRE( alice_collateral.collateral == collateral.amount - borrow.collateral );
      BOOST_REQUIRE( alice_collateral.last_updated == now() );

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Updating Borrowing order to increase loan debt" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when updating borrowing order to remove collateral" );

      borrow.collateral = asset( 0, SYMBOL_COIN );
      
      tx.operations.push_back( borrow );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when updating borrowing order to remove collateral" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when updating borrowing order to less than minimum collateral ratio" );

      borrow.amount = asset( 10000 * BLOCKCHAIN_PRECISION, SYMBOL_USD );
      borrow.collateral = asset( 10000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      
      tx.operations.push_back( borrow );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when updating borrowing order to less than minimum collateral ratio" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Withdrawing funds from credit pool" );

      credit_pool_withdraw_operation withdraw;

      withdraw.signatory = "bob";
      withdraw.account = "bob";
      withdraw.amount = asset( 5000 * BLOCKCHAIN_PRECISION, usd_credit_symbol );
      withdraw.validate();

      tx.operations.push_back( withdraw );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_REQUIRE( usd_credit_pool.base_symbol == lend.amount.symbol );
      BOOST_REQUIRE( usd_credit_pool.credit_symbol == withdraw.amount.symbol );
      BOOST_REQUIRE( usd_credit_pool.base_balance == lend.amount - borrow.amount - withdraw.amount );
      BOOST_REQUIRE( usd_credit_pool.borrowed_balance == borrow.amount );
   
      tx.operations.clear();
      tx.signatures.clear();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Withdrawing funds from credit pool" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when withdrawing more funds than balance" );

      asset usd_credit_balance = db.get_liquid_balance( "bob", usd_credit_symbol );

      withdraw.amount = asset( 2 * usd_credit_balance.amount, usd_credit_symbol );

      tx.operations.push_back( withdraw );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when withdrawing more funds than balance" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when withdrawing more funds than available from pool" );

      withdraw.amount = usd_credit_balance;

      tx.operations.push_back( withdraw );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when withdrawing funds more funds than available from pool" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when withdrawing funds using reserve asset instead of credit pool asset" );

      withdraw.amount = asset( 50000 * BLOCKCHAIN_PRECISION, SYMBOL_USD );

      tx.operations.push_back( withdraw );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when withdrawing funds using reserve asset instead of credit pool asset" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Closing out loan after 7 days" );

      generate_blocks( 7 * BLOCKS_PER_DAY );

      borrow.amount = asset( 0, SYMBOL_USD );
      borrow.collateral = asset( 0, SYMBOL_COIN );
      
      tx.operations.push_back( borrow );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_REQUIRE( usd_credit_pool.borrowed_balance.amount == 0 );

      const auto& loan_idx = db.get_index< credit_loan_index >().indices().get< by_loan_id >();
      auto loan_itr = loan_idx.find( std::make_tuple( "alice", string( "7d8f6c1a-0409-416f-9e07-f60c46381a92" ) ) );

      BOOST_REQUIRE( loan_itr == loan_idx.end() );

      BOOST_REQUIRE( alice_collateral.collateral == collateral.amount );
      BOOST_REQUIRE( alice_collateral.last_updated == now() );
      BOOST_REQUIRE( alice.loan_default_balance.amount == 0  );

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Closing out loan after 7 days" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Loan Default liquidation" );

      borrow.amount = asset( 7500 * BLOCKCHAIN_PRECISION, SYMBOL_USD );
      borrow.collateral = asset( 10000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      borrow.loan_id = "ab853d22-e03d-46f5-9437-93f5fb4ea7df";
      
      tx.operations.push_back( borrow );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_REQUIRE( usd_credit_pool.borrowed_balance.amount == 0 );

      const credit_loan_object& alice_loan2 = db.get_loan( "alice", string( "ab853d22-e03d-46f5-9437-93f5fb4ea7df" ) );

      BOOST_REQUIRE( alice_loan2.debt == borrow.amount );
      BOOST_REQUIRE( alice_loan2.collateral == borrow.collateral );

      BOOST_REQUIRE( alice_collateral.collateral == collateral.amount );
      BOOST_REQUIRE( alice_collateral.last_updated == now() );

      feed.signatory = "alice";
      feed.publisher = "alice";
      feed.symbol = SYMBOL_USD;
      feed.feed.settlement_price = price( asset(1,SYMBOL_USD),asset(2,SYMBOL_COIN) );    // 1:2 ratio, price halved
      
      tx.operations.push_back( feed );
      tx.set_expiration( now() + fc::seconds( MAX_TIME_UNTIL_EXPIRATION ) );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      feed.signatory = "bob";
      feed.publisher = "bob";

      tx.operations.push_back( feed );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      feed.signatory = "candice";
      feed.publisher = "candice";

      tx.operations.push_back( feed );
      tx.sign( candice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      feed.signatory = "dan";
      feed.publisher = "dan";

      tx.operations.push_back( feed );
      tx.sign( dan_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      generate_block();     // Price halved, loan is insolvent, and is liquidated and network credit is issued to repurchase usd

      loan_itr = loan_idx.find( std::make_tuple( "alice", string( "ab853d22-e03d-46f5-9437-93f5fb4ea7df" ) ) );

      BOOST_REQUIRE( loan_itr == loan_idx.end() );
      BOOST_REQUIRE( alice.loan_default_balance.amount > 0  );

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Loan Default liquidation" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when borrowing with loan default balance outstanding" );

      borrow.amount = asset( 7500 * BLOCKCHAIN_PRECISION, SYMBOL_USD );
      borrow.collateral = asset( 10000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      borrow.loan_id = "f44cf0e4-66d8-4225-a93a-80ca69300606";
      
      tx.operations.push_back( borrow );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when borrowing with loan default balance outstanding" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Success when loan default is repaid" );

      collateral.signatory = "alice";
      collateral.account = "alice";
      collateral.amount = alice.loan_default_balance;

      tx.operations.push_back( collateral );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      BOOST_REQUIRE( alice.loan_default_balance.amount == 0  );

      borrow.amount = asset( 7500 * BLOCKCHAIN_PRECISION, SYMBOL_USD );
      borrow.collateral = asset( 10000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      borrow.loan_id = "f44cf0e4-66d8-4225-a93a-80ca69300606";
      
      tx.operations.push_back( borrow );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );
      
      tx.operations.clear();
      tx.signatures.clear();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Success when loan default is repaid" );

      BOOST_TEST_MESSAGE( "├── Testing: CREDIT POOL OPERATION SEQUENCE" );
   }
   FC_LOG_AND_RETHROW()
}


BOOST_AUTO_TEST_CASE( prediction_pool_operation_sequence_test )
{
   try
   {
      BOOST_TEST_MESSAGE( "├── Testing: PREDICTION POOL OPERATION SEQUENCE" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Create prediction pool" );

      fund( INIT_ACCOUNT, asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );

      ACTORS( (alice)(bob)(candice)(dan) );

      fund( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund_stake( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund_stake( "alice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );

      fund( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund_stake( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund_stake( "bob", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      
      fund( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund_stake( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund_stake( "candice", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );

      fund( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund_stake( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      fund( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      fund_stake( "dan", asset( 100000 * BLOCKCHAIN_PRECISION, SYMBOL_USD ) );
      
      signed_transaction tx;

      prediction_pool_create_operation create;

      create.signatory = "alice";
      create.account = "alice";
      create.prediction_symbol = "PREDICTION";
      create.collateral_symbol = SYMBOL_COIN;
      create.outcome_assets = { "YES", "NO" };
      create.outcome_details = { "The predicted event will happen.", "The predicted event will not happen." };
      create.display_symbol = "Prediction market asset success.";
      create.json = "{ \"valid\": true }";
      create.url = "https://www.url.com";
      create.details = "Details";
      create.outcome_time = now() + fc::days(8);
      create.prediction_bond = asset( 100 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      create.validate();

      tx.operations.push_back( create );
      tx.set_expiration( now() + fc::seconds( MAX_TIME_UNTIL_EXPIRATION ) );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      const asset_prediction_pool_object& prediction = db.get_prediction_pool( "PREDICTION" );
      const asset_object& prediction_asset = db.get_asset( "PREDICTION" );

      BOOST_REQUIRE( create.account == prediction.issuer );
      BOOST_REQUIRE( create.prediction_symbol == prediction.prediction_symbol );
      BOOST_REQUIRE( create.collateral_symbol == prediction.collateral_symbol );

      for( size_t i = 0; i < prediction.outcome_assets.size(); i++ )
      {
         BOOST_REQUIRE( create.outcome_assets[i] == prediction.outcome_assets[i] );
         BOOST_REQUIRE( create.outcome_details[i] == to_string( prediction.outcome_details[i] ) );
      }

      BOOST_REQUIRE( create.display_symbol == to_string( prediction_asset.display_symbol ) );
      BOOST_REQUIRE( create.json == to_string( prediction.json ) );
      BOOST_REQUIRE( create.url == to_string( prediction.url ) );
      BOOST_REQUIRE( create.details == to_string( prediction.details ) );
      BOOST_REQUIRE( create.outcome_time == prediction.outcome_time );
      BOOST_REQUIRE( create.prediction_bond == prediction.prediction_bond_pool );

      BOOST_TEST_MESSAGE( "│   ├── Passed: Create prediction position" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Failure when making a prediction pool bond without sufficient balance" );

      create.signatory = "alice";
      create.account = "alice";
      create.prediction_symbol = "PREDICTIONB";
      create.collateral_symbol = SYMBOL_COIN;
      create.outcome_assets = { "YES", "NO" };
      create.outcome_assets = { "The predicted event will happen.", "The predicted event will not happen." };
      create.display_symbol = "Prediction market asset failure.";
      create.json = "{ \"valid\": true }";
      create.url = "https://www.url.com";
      create.details = "Details";
      create.outcome_time = now() + fc::days(30);
      create.prediction_bond = asset( 1000000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      create.validate();

      tx.operations.push_back( create );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      REQUIRE_THROW( db.push_transaction( tx, 0 ), fc::exception );

      tx.operations.clear();
      tx.signatures.clear();

      validate_database();

      BOOST_TEST_MESSAGE( "│   ├── Passed: Failure when making a prediction pool bond without sufficient balance" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Exchange funds with prediction pool" );

      prediction_pool_exchange_operation exchange;

      exchange.signatory = "bob";
      exchange.account = "bob";
      exchange.amount = asset( 2000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      exchange.prediction_asset = "PREDICTION";
      exchange.exchange_base = false;
      exchange.withdraw = false;
      exchange.validate();

      tx.operations.push_back( exchange );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      exchange.signatory = "bob";
      exchange.account = "bob";
      exchange.amount = asset( 1000 * BLOCKCHAIN_PRECISION, SYMBOL_COIN );
      exchange.prediction_asset = "PREDICTION";
      exchange.exchange_base = false;
      exchange.withdraw = true;
      exchange.validate();

      tx.operations.push_back( exchange );
      tx.sign( bob_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      transfer_operation transfer;

      transfer.signatory = "bob";
      transfer.from = "bob";
      transfer.to = "candice";
      transfer.amount = asset( 1000 * BLOCKCHAIN_PRECISION, "PREDICTION.NO" );
      transfer.memo = "Outcome asset";
      transfer.validate();

      BOOST_REQUIRE( prediction.collateral_pool == exchange.amount );

      BOOST_TEST_MESSAGE( "│   ├── Passed: Exchange funds with prediction pool" );

      BOOST_TEST_MESSAGE( "│   ├── Testing: Resolve Prediction Pool" );

      asset init_alice_liquid_coin = get_liquid_balance( "alice", SYMBOL_COIN );

      generate_blocks( prediction.outcome_time + fc::minutes(1) );

      prediction_pool_resolve_operation resolve;

      resolve.signatory = "alice";
      resolve.account = "alice";
      resolve.amount = asset( 100 * BLOCKCHAIN_PRECISION, "PREDICTION" );
      resolve.resolution_outcome = "PREDICTION.YES";
      resolve.validate();
      
      tx.operations.push_back( resolve );
      tx.sign( alice_private_active_key, db.get_chain_id() );
      db.push_transaction( tx, 0 );

      tx.operations.clear();
      tx.signatures.clear();

      const asset_prediction_pool_resolution_object& resolution = db.get_prediction_pool_resolution( "alice", "PREDICTION" );

      BOOST_REQUIRE( resolution.account == resolve.account );
      BOOST_REQUIRE( resolution.amount == resolve.amount );
      BOOST_REQUIRE( resolution.prediction_symbol == resolve.amount.symbol );
      BOOST_REQUIRE( resolution.resolution_outcome == resolve.resolution_outcome );

      validate_database();

      generate_blocks( prediction.resolution_time + fc::minutes(1) );

      asset alice_liquid_coin = get_liquid_balance( "alice", SYMBOL_COIN );
      asset bob_liquid_prediction = get_liquid_balance( "bob", "PREDICTION.YES" );
      asset candice_liquid_prediction = get_liquid_balance( "candice", "PREDICTION.NO" );

      const asset_prediction_pool_object* pool_ptr = db.find_prediction_pool( "PREDICTION" );
      const asset_prediction_pool_resolution_object* resolution_ptr = db.find_prediction_pool_resolution( "alice", "PREDICTION" );
      
      BOOST_REQUIRE( pool_ptr == nullptr );
      BOOST_REQUIRE( resolution_ptr == nullptr );
      BOOST_REQUIRE( alice_liquid_coin == init_alice_liquid_coin + asset( 100 * BLOCKCHAIN_PRECISION, SYMBOL_COIN ) );
      BOOST_REQUIRE( bob_liquid_prediction.amount == 0 );
      BOOST_REQUIRE( candice_liquid_prediction.amount == 0 );

      BOOST_TEST_MESSAGE( "│   ├── Passed: Resolve Prediction Pool" );

      BOOST_TEST_MESSAGE( "├── Testing: PREDICTION POOL OPERATION SEQUENCE" );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()