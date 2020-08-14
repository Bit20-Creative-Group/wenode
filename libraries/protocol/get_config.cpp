#include <node/protocol/get_config.hpp>
#include <node/protocol/config.hpp>
#include <node/protocol/asset.hpp>
#include <node/protocol/types.hpp>
#include <node/protocol/version.hpp>

namespace node { namespace protocol {

fc::variant_object get_config()
{
   fc::mutable_variant_object result;

   result["BLOCKCHAIN_VERSION"] = BLOCKCHAIN_VERSION;
   result["BLOCKCHAIN_HARDFORK_VERSION"] = BLOCKCHAIN_HARDFORK_VERSION;

   result["SHOW_PRIVATE_KEYS"] = SHOW_PRIVATE_KEYS;
   result["GEN_PRIVATE_KEY"] = GEN_PRIVATE_KEY;

   result["INIT_PUBLIC_KEY_STR"] = INIT_PUBLIC_KEY_STR;
   result["OWNER_AUTH_RECOVERY_PERIOD"] = OWNER_AUTH_RECOVERY_PERIOD;
   result["OWNER_UPDATE_LIMIT"] = OWNER_UPDATE_LIMIT;
   result["ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD"] = ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD;

   #ifdef SHOW_PRIVATE_KEYS
      #if SHOW_PRIVATE_KEYS
         #ifdef INIT_PRIVATE_KEY
         result["INIT_PRIVATE_KEY"] = INIT_PRIVATE_KEY;
         #endif
      #endif
      // do not expose private key, period. - from steem devs
      // we need this line present but inactivated so CI check for all constants in config.hpp doesn't complain.
   #endif

   #ifdef IS_TEST_NET
      result[ "IS_TEST_NET" ] = true;
   #else
      result[ "IS_TEST_NET" ] = false;
   #endif

   result["CHAIN_ID"] = CHAIN_ID;
   result["ADDRESS_PREFIX"] = ADDRESS_PREFIX;

   result["BLOCKCHAIN_PRECISION"] = BLOCKCHAIN_PRECISION;
   result["BLOCKCHAIN_PRECISION_DIGITS"] = BLOCKCHAIN_PRECISION_DIGITS;

   result["MECOIN_SYMBOL"] = MECOIN_SYMBOL;
   result["WEYOUME_SYMBOL"] = WEYOUME_SYMBOL;
   result["MEUSD_SYMBOL"] = MEUSD_SYMBOL;
   result["MECREDIT_SYMBOL"] = MECREDIT_SYMBOL;

   result["LIQUIDITY_ASSET_PREFIX"] = LIQUIDITY_ASSET_PREFIX;
   result["CREDIT_ASSET_PREFIX"] = CREDIT_ASSET_PREFIX;
   result["OPTION_ASSET_CALL"] = OPTION_ASSET_CALL;
   result["OPTION_ASSET_PUT"] = OPTION_ASSET_PUT;

   result["SYMBOL_COIN"] = SYMBOL_COIN;
   result["SYMBOL_EQUITY"] = SYMBOL_EQUITY;
   result["SYMBOL_USD"] = SYMBOL_USD;
   result["SYMBOL_CREDIT"] = SYMBOL_CREDIT;

   result["COIN_DETAILS"] = COIN_DETAILS;
   result["EQUITY_DETAILS"] = EQUITY_DETAILS;
   result["USD_DETAILS"] = USD_DETAILS;
   result["CREDIT_DETAILS"] = CREDIT_DETAILS;

   result["MILLION"] = MILLION;
   result["BILLION"] = BILLION;
   result["PERCENT_100"] = PERCENT_100;
   result["PERCENT_1"] = PERCENT_1;
   result["PERCENT_10_OF_PERCENT_1"] = PERCENT_10_OF_PERCENT_1;

   result["GENESIS_TIME"] = GENESIS_TIME;
   result["MINING_TIME"] = MINING_TIME;

   result["INIT_COIN_SUPPLY"] = INIT_COIN_SUPPLY;
   result["INIT_EQUITY_SUPPLY"] = INIT_EQUITY_SUPPLY;
   result["MAX_ASSET_SUPPLY"] = MAX_ASSET_SUPPLY;

   result["BLOCK_INTERVAL"] = BLOCK_INTERVAL;
   result["BLOCKS_PER_YEAR"] = BLOCKS_PER_YEAR;
   result["BLOCKS_PER_HOUR"] = BLOCKS_PER_HOUR;
   result["BLOCKS_PER_DAY"] = BLOCKS_PER_DAY;
   result["BLOCKS_PER_MINUTE"] = BLOCKS_PER_MINUTE;

   result["ANNUAL_COIN_ISSUANCE"] = ANNUAL_COIN_ISSUANCE;
   result["BLOCK_REWARD"] = BLOCK_REWARD;
   result["MAX_ACCEPTED_PAYOUT"] = MAX_ACCEPTED_PAYOUT;

   result["PRODUCER_TICK_INTERVAL"] = PRODUCER_TICK_INTERVAL;
   result["MINING_TICK_INTERVAL"] = MINING_TICK_INTERVAL;
   result["VALIDATION_TICK_INTERVAL"] = VALIDATION_TICK_INTERVAL;
   result["IRREVERSIBLE_THRESHOLD"] = IRREVERSIBLE_THRESHOLD;
   result["POW_TARGET_TIME"] = POW_TARGET_TIME;
   result["POW_DECAY_TIME"] = POW_DECAY_TIME;
   result["INIT_RECENT_POW"] = INIT_RECENT_POW;
   result["POW_UPDATE_BLOCK_INTERVAL"] = POW_UPDATE_BLOCK_INTERVAL;
   result["POA_BLOCK_INTERVAL"] = POA_BLOCK_INTERVAL;
   result["TXN_STAKE_BLOCK_INTERVAL"] = TXN_STAKE_BLOCK_INTERVAL;
   result["TXN_STAKE_DECAY_TIME"] = TXN_STAKE_DECAY_TIME;
   result["NETWORK_OFFICER_BLOCK_INTERVAL"] = NETWORK_OFFICER_BLOCK_INTERVAL;
   result["NETWORK_OFFICER_ACTIVE_SET"] = NETWORK_OFFICER_ACTIVE_SET;
   result["EXECUTIVE_BOARD_BLOCK_INTERVAL"] = EXECUTIVE_BOARD_BLOCK_INTERVAL;
   result["MIN_EXEC_CREDIT_PRICE"] = MIN_EXEC_CREDIT_PRICE;
   result["SUPERNODE_BLOCK_INTERVAL"] = SUPERNODE_BLOCK_INTERVAL;
   result["SUPERNODE_DECAY_TIME"] = SUPERNODE_DECAY_TIME;

   result["GOVERNANCE_VOTE_THRESHOLD_PERCENT"] = GOVERNANCE_VOTE_THRESHOLD_PERCENT;
   result["GOVERNANCE_VOTE_THRESHOLD_AMOUNT"] = GOVERNANCE_VOTE_THRESHOLD_AMOUNT;
   result["GOVERNANCE_VOTE_THRESHOLD_PRODUCERS"] = GOVERNANCE_VOTE_THRESHOLD_PRODUCERS;
   result["ENTERPRISE_VOTE_THRESHOLD_PERCENT"] = ENTERPRISE_VOTE_THRESHOLD_PERCENT;
   result["ENTERPRISE_VOTE_THRESHOLD_AMOUNT"] = ENTERPRISE_VOTE_THRESHOLD_AMOUNT;
   result["ENTERPRISE_VOTE_THRESHOLD_PRODUCERS"] = ENTERPRISE_VOTE_THRESHOLD_PRODUCERS;
   result["OFFICER_VOTE_THRESHOLD_PERCENT"] = OFFICER_VOTE_THRESHOLD_PERCENT;
   result["OFFICER_VOTE_THRESHOLD_AMOUNT"] = OFFICER_VOTE_THRESHOLD_AMOUNT;
   result["OFFICER_VOTE_THRESHOLD_PRODUCERS"] = OFFICER_VOTE_THRESHOLD_PRODUCERS;
   result["EXECUTIVE_VOTE_THRESHOLD_PERCENT"] = EXECUTIVE_VOTE_THRESHOLD_PERCENT;
   result["EXECUTIVE_VOTE_THRESHOLD_AMOUNT"] = EXECUTIVE_VOTE_THRESHOLD_AMOUNT;
   result["EXECUTIVE_VOTE_THRESHOLD_PRODUCERS"] = EXECUTIVE_VOTE_THRESHOLD_PRODUCERS;
   result["MAX_ACCOUNT_VOTES"] = MAX_ACCOUNT_VOTES;

   result["SET_UPDATE_BLOCK_INTERVAL"] = SET_UPDATE_BLOCK_INTERVAL;
   result["ENTERPRISE_BLOCK_INTERVAL"] = ENTERPRISE_BLOCK_INTERVAL;
   result["STABLECOIN_BLOCK_INTERVAL"] = STABLECOIN_BLOCK_INTERVAL;
   result["VOTE_RECHARGE_TIME"] = VOTE_RECHARGE_TIME;
   result["VIEW_RECHARGE_TIME"] = VIEW_RECHARGE_TIME;
   result["SHARE_RECHARGE_TIME"] = SHARE_RECHARGE_TIME;
   result["COMMENT_RECHARGE_TIME"] = COMMENT_RECHARGE_TIME;
   result["VOTE_RESERVE_RATE"] = VOTE_RESERVE_RATE;
   result["VIEW_RESERVE_RATE"] = VIEW_RESERVE_RATE;
   result["SHARE_RESERVE_RATE"] = SHARE_RESERVE_RATE;
   result["COMMENT_RESERVE_RATE"] = COMMENT_RESERVE_RATE;
   result["CURATION_AUCTION_DECAY_TIME"] = CURATION_AUCTION_DECAY_TIME;
   result["VOTE_CURATION_DECAY"] = VOTE_CURATION_DECAY;
   result["VIEW_CURATION_DECAY"] = VIEW_CURATION_DECAY;
   result["SHARE_CURATION_DECAY"] = SHARE_CURATION_DECAY;
   result["COMMENT_CURATION_DECAY"] = COMMENT_CURATION_DECAY;

   result["DECLINE_VOTING_RIGHTS_DURATION"] = DECLINE_VOTING_RIGHTS_DURATION;
   result["MIN_COMMUNITY_CREATE_INTERVAL"] = MIN_COMMUNITY_CREATE_INTERVAL;
   result["MIN_ASSET_CREATE_INTERVAL"] = MIN_ASSET_CREATE_INTERVAL;
   result["MIN_COMMUNITY_UPDATE_INTERVAL"] = MIN_COMMUNITY_UPDATE_INTERVAL;
   result["MIN_ASSET_UPDATE_INTERVAL"] = MIN_ASSET_UPDATE_INTERVAL;
   
   result["GENESIS_PRODUCER_AMOUNT"] = GENESIS_PRODUCER_AMOUNT;
   result["TOTAL_PRODUCERS"] = TOTAL_PRODUCERS;
   result["GENESIS_EXTRA_PRODUCERS"] = GENESIS_EXTRA_PRODUCERS;
   result["DPOS_VOTING_PRODUCERS"] = DPOS_VOTING_PRODUCERS;
   result["POW_MINING_PRODUCERS"] = POW_MINING_PRODUCERS;
   result["DPOS_VOTING_ADDITIONAL_PRODUCERS"] = DPOS_VOTING_ADDITIONAL_PRODUCERS;
   result["POW_MINING_ADDITIONAL_PRODUCERS"] = POW_MINING_ADDITIONAL_PRODUCERS;

   result["HARDFORK_REQUIRED_PRODUCERS"] = HARDFORK_REQUIRED_PRODUCERS;
   result["MAX_TIME_UNTIL_EXPIRATION"] = MAX_TIME_UNTIL_EXPIRATION;
   result["MAX_MEMO_SIZE"] = MAX_MEMO_SIZE;
   result["MAX_PROXY_RECURSION_DEPTH"] = MAX_PROXY_RECURSION_DEPTH;
   result["COIN_UNSTAKE_INTERVALS"] = COIN_UNSTAKE_INTERVALS;
   result["STAKE_WITHDRAW_INTERVAL"] = STAKE_WITHDRAW_INTERVAL;
   result["MAX_WITHDRAW_ROUTES"] = MAX_WITHDRAW_ROUTES;
   result["SAVINGS_WITHDRAW_TIME"] = SAVINGS_WITHDRAW_TIME;
   result["SAVINGS_WITHDRAW_REQUEST_LIMIT"] = SAVINGS_WITHDRAW_REQUEST_LIMIT;
   result["MAX_ASSET_STAKE_INTERVALS"] = MAX_ASSET_STAKE_INTERVALS;
   result["MAX_ASSET_UNSTAKE_INTERVALS"] = MAX_ASSET_UNSTAKE_INTERVALS;

   result["MAX_VOTE_CHANGES"] = MAX_VOTE_CHANGES;
   result["MIN_VOTE_INTERVAL"] = MIN_VOTE_INTERVAL;
   result["MIN_VIEW_INTERVAL"] = MIN_VIEW_INTERVAL;
   result["MIN_SHARE_INTERVAL"] = MIN_SHARE_INTERVAL;
   result["MIN_ROOT_COMMENT_INTERVAL"] = MIN_ROOT_COMMENT_INTERVAL;
   result["MIN_REPLY_INTERVAL"] = MIN_REPLY_INTERVAL;

   result["BANDWIDTH_AVERAGE_WINDOW"] = BANDWIDTH_AVERAGE_WINDOW;
   result["BANDWIDTH_PRECISION"] = BANDWIDTH_PRECISION;
   result["MAX_BODY_SIZE"] = MAX_BODY_SIZE;
   result["MAX_COMMENT_DEPTH"] = MAX_COMMENT_DEPTH;
   result["SOFT_MAX_COMMENT_DEPTH"] = SOFT_MAX_COMMENT_DEPTH;
   result["MAX_RESERVE_RATIO"] = MAX_RESERVE_RATIO;
   result["MIN_ACCOUNT_CREATION_FEE"] = MIN_ACCOUNT_CREATION_FEE;
   result["MIN_ASSET_COIN_LIQUIDITY"] = MIN_ASSET_COIN_LIQUIDITY;
   result["MIN_ASSET_USD_LIQUIDITY"] = MIN_ASSET_USD_LIQUIDITY;
   result["CREATE_ACCOUNT_DELEGATION_RATIO"] = CREATE_ACCOUNT_DELEGATION_RATIO;
   result["CREATE_ACCOUNT_DELEGATION_TIME"] = CREATE_ACCOUNT_DELEGATION_TIME;
   result["EQUIHASH_N"] = EQUIHASH_N;
   result["EQUIHASH_K"] = EQUIHASH_K;

   result["CONTENT_REWARD_PERCENT"] = CONTENT_REWARD_PERCENT;
   result["EQUITY_REWARD_PERCENT"] = EQUITY_REWARD_PERCENT;
   result["PRODUCER_REWARD_PERCENT"] = PRODUCER_REWARD_PERCENT;
   result["SUPERNODE_REWARD_PERCENT"] = SUPERNODE_REWARD_PERCENT;
   result["POWER_REWARD_PERCENT"] = POWER_REWARD_PERCENT;
   result["COMMUNITY_FUND_REWARD_PERCENT"] = COMMUNITY_FUND_REWARD_PERCENT;
   result["DEVELOPMENT_REWARD_PERCENT"] = DEVELOPMENT_REWARD_PERCENT;
   result["ADVOCACY_REWARD_PERCENT"] = ADVOCACY_REWARD_PERCENT;
   result["MARKETING_REWARD_PERCENT"] = MARKETING_REWARD_PERCENT;
   result["ACTIVITY_REWARD_PERCENT"] = ACTIVITY_REWARD_PERCENT;

   result["AUTHOR_REWARD_PERCENT"] = AUTHOR_REWARD_PERCENT;
   result["VOTE_REWARD_PERCENT"] = VOTE_REWARD_PERCENT;
   result["VIEW_REWARD_PERCENT"] = VIEW_REWARD_PERCENT;
   result["SHARE_REWARD_PERCENT"] = SHARE_REWARD_PERCENT;
   result["COMMENT_REWARD_PERCENT"] = COMMENT_REWARD_PERCENT;
   result["STORAGE_REWARD_PERCENT"] = STORAGE_REWARD_PERCENT;
   result["MODERATOR_REWARD_PERCENT"] = MODERATOR_REWARD_PERCENT;
   result["CONTENT_REWARD_INTERVAL_AMOUNT"] = CONTENT_REWARD_INTERVAL_AMOUNT;
   result["CONTENT_REWARD_INTERVAL_HOURS"] = CONTENT_REWARD_INTERVAL_HOURS;
   result["CONTENT_CONSTANT"] = CONTENT_CONSTANT;
   result["MIN_PAYOUT_USD"] = MIN_PAYOUT_USD;

   result["PRODUCER_BLOCK_PERCENT"] = PRODUCER_BLOCK_PERCENT;
   result["PRODUCER_VALIDATOR_PERCENT"] = PRODUCER_VALIDATOR_PERCENT;
   result["PRODUCER_TXN_STAKE_PERCENT"] = PRODUCER_TXN_STAKE_PERCENT;
   result["PRODUCER_WORK_PERCENT"] = PRODUCER_WORK_PERCENT;
   result["PRODUCER_ACTIVITY_PERCENT"] = PRODUCER_ACTIVITY_PERCENT;

   result["GOVERNANCE_SHARE_PERCENT"] = GOVERNANCE_SHARE_PERCENT;
   result["REFERRAL_SHARE_PERCENT"] = REFERRAL_SHARE_PERCENT;
   result["REWARD_LIQUID_PERCENT"] = REWARD_LIQUID_PERCENT;
   result["REWARD_STAKED_PERCENT"] = REWARD_STAKED_PERCENT;

   result["TRADING_FEE_PERCENT"] = TRADING_FEE_PERCENT;
   result["NETWORK_TRADING_FEE_PERCENT"] = NETWORK_TRADING_FEE_PERCENT;
   result["MAKER_TRADING_FEE_PERCENT"] = MAKER_TRADING_FEE_PERCENT;
   result["TAKER_TRADING_FEE_PERCENT"] = TAKER_TRADING_FEE_PERCENT;

   result["ADVERTISING_FEE_PERCENT"] = ADVERTISING_FEE_PERCENT;
   result["NETWORK_ADVERTISING_FEE_PERCENT"] = NETWORK_ADVERTISING_FEE_PERCENT;
   result["DEMAND_ADVERTISING_FEE_PERCENT"] = DEMAND_ADVERTISING_FEE_PERCENT;
   result["AUDIENCE_ADVERTISING_FEE_PERCENT"] = AUDIENCE_ADVERTISING_FEE_PERCENT;

   result["MEMBERSHIP_FEE_BASE"] = MEMBERSHIP_FEE_BASE;
   result["MEMBERSHIP_FEE_MID"] = MEMBERSHIP_FEE_MID;
   result["MEMBERSHIP_FEE_TOP"] = MEMBERSHIP_FEE_TOP;
   result["NETWORK_MEMBERSHIP_FEE_PERCENT"] = NETWORK_MEMBERSHIP_FEE_PERCENT;
   result["INTERFACE_MEMBERSHIP_FEE_PERCENT"] = INTERFACE_MEMBERSHIP_FEE_PERCENT;
   result["PARTNERS_MEMBERSHIP_FEE_PERCENT"] = PARTNERS_MEMBERSHIP_FEE_PERCENT;
   
   result["PREMIUM_FEE_PERCENT"] = PREMIUM_FEE_PERCENT;
   result["NETWORK_PREMIUM_FEE_PERCENT"] = NETWORK_PREMIUM_FEE_PERCENT;
   result["INTERFACE_PREMIUM_FEE_PERCENT"] = INTERFACE_PREMIUM_FEE_PERCENT;
   result["NODE_PREMIUM_FEE_PERCENT"] = NODE_PREMIUM_FEE_PERCENT;

   result["MARKETPLACE_FEE_PERCENT"] = MARKETPLACE_FEE_PERCENT;
   result["NETWORK_MARKETPLACE_FEE_PERCENT"] = NETWORK_MARKETPLACE_FEE_PERCENT;
   result["BUY_INTERFACE_MARKETPLACE_FEE_PERCENT"] = BUY_INTERFACE_MARKETPLACE_FEE_PERCENT;
   result["SELL_INTERFACE_MARKETPLACE_FEE_PERCENT"] = SELL_INTERFACE_MARKETPLACE_FEE_PERCENT;
   result["DISPUTE_FEE_PERCENT"] = DISPUTE_FEE_PERCENT;
   result["NETWORK_DISPUTE_FEE_PERCENT"] = NETWORK_DISPUTE_FEE_PERCENT;
   result["BUYER_MEDIATOR_DISPUTE_FEE_PERCENT"] = BUYER_MEDIATOR_DISPUTE_FEE_PERCENT;
   result["SELLER_MEDIATOR_DISPUTE_FEE_PERCENT"] = SELLER_MEDIATOR_DISPUTE_FEE_PERCENT;
   result["ALLOCATED_MEDIATOR_DISPUTE_FEE_PERCENT"] = ALLOCATED_MEDIATOR_DISPUTE_FEE_PERCENT;
   result["ESCROW_BOND_PERCENT"] = ESCROW_BOND_PERCENT;
   result["ESCROW_DISPUTE_DURATION"] = ESCROW_DISPUTE_DURATION;
   result["ESCROW_DISPUTE_MEDIATOR_AMOUNT"] = ESCROW_DISPUTE_MEDIATOR_AMOUNT;

   result["SUBSCRIPTION_FEE_PERCENT"] = SUBSCRIPTION_FEE_PERCENT;
   result["NETWORK_SUBSCRIPTION_FEE_PERCENT"] = NETWORK_SUBSCRIPTION_FEE_PERCENT;
   result["INTERFACE_SUBSCRIPTION_FEE_PERCENT"] = INTERFACE_SUBSCRIPTION_FEE_PERCENT;
   result["NODE_SUBSCRIPTION_FEE_PERCENT"] = NODE_SUBSCRIPTION_FEE_PERCENT;

   result["MIN_ACTIVITY_PRODUCERS"] = MIN_ACTIVITY_PRODUCERS;
   result["ACTIVITY_BOOST_STANDARD_PERCENT"] = ACTIVITY_BOOST_STANDARD_PERCENT;
   result["ACTIVITY_BOOST_MID_PERCENT"] = ACTIVITY_BOOST_MID_PERCENT;
   result["ACTIVITY_BOOST_TOP_PERCENT"] = ACTIVITY_BOOST_TOP_PERCENT;
   result["RECENT_REWARD_DECAY_RATE"] = RECENT_REWARD_DECAY_RATE;

   result["MIN_ACCOUNT_NAME_LENGTH"] = MIN_ACCOUNT_NAME_LENGTH;
   result["MAX_ACCOUNT_NAME_LENGTH"] = MAX_ACCOUNT_NAME_LENGTH;
   result["MIN_ASSET_SYMBOL_LENGTH"] = MIN_ASSET_SYMBOL_LENGTH;
   result["MAX_ASSET_SYMBOL_LENGTH"] = MAX_ASSET_SYMBOL_LENGTH;
   result["MIN_PERMLINK_LENGTH"] = MIN_PERMLINK_LENGTH;
   result["MAX_PERMLINK_LENGTH"] = MAX_PERMLINK_LENGTH;
   result["MAX_STRING_LENGTH"] = MAX_STRING_LENGTH;
   result["MAX_TEXT_POST_LENGTH"] = MAX_TEXT_POST_LENGTH;
   result["MAX_SIG_CHECK_DEPTH"] = MAX_SIG_CHECK_DEPTH;
   result["MAX_GOV_ACCOUNTS"] = MAX_GOV_ACCOUNTS;
   result["MAX_EXEC_VOTES"] = MAX_EXEC_VOTES;
   result["MAX_OFFICER_VOTES"] = MAX_OFFICER_VOTES;
   result["MAX_EXEC_BUDGET"] = MAX_EXEC_BUDGET;
   result["COLLATERAL_RATIO_DENOM"] = COLLATERAL_RATIO_DENOM;
   result["MIN_COLLATERAL_RATIO"] = MIN_COLLATERAL_RATIO;
   result["MAX_COLLATERAL_RATIO"] = MAX_COLLATERAL_RATIO;
   result["MAINTENANCE_COLLATERAL_RATIO"] = MAINTENANCE_COLLATERAL_RATIO;
   result["MAX_SHORT_SQUEEZE_RATIO"] = MAX_SHORT_SQUEEZE_RATIO;

   result["MARGIN_OPEN_RATIO"] = MARGIN_OPEN_RATIO;
   result["MARGIN_LIQUIDATION_RATIO"] = MARGIN_LIQUIDATION_RATIO;
   result["CREDIT_OPEN_RATIO"] = CREDIT_OPEN_RATIO;
   result["CREDIT_LIQUIDATION_RATIO"] = CREDIT_LIQUIDATION_RATIO;
   result["CREDIT_INTEREST_RATE"] = CREDIT_INTEREST_RATE;
   result["CREDIT_MIN_INTEREST"] = CREDIT_MIN_INTEREST;
   result["CREDIT_VARIABLE_INTEREST"] = CREDIT_VARIABLE_INTEREST;
   result["CREDIT_INTERVAL_BLOCKS"] = CREDIT_INTERVAL_BLOCKS;
   result["INTEREST_MIN_AMOUNT"] = INTEREST_MIN_AMOUNT;
   result["INTEREST_MIN_INTERVAL"] = INTEREST_MIN_INTERVAL;
   result["MARKET_MAX_CREDIT_RATIO"] = MARKET_MAX_CREDIT_RATIO;
   result["INTEREST_FEE_PERCENT"] = INTEREST_FEE_PERCENT;

   result["EQUITY_MIN_PRODUCERS"] = EQUITY_MIN_PRODUCERS;
   result["EQUITY_BOOST_PRODUCERS"] = EQUITY_BOOST_PRODUCERS;
   result["EQUITY_ACTIVITY_TIME"] = EQUITY_ACTIVITY_TIME;
   result["EQUITY_BOOST_ACTIVITY"] = EQUITY_BOOST_ACTIVITY;
   result["EQUITY_BOOST_BALANCE"] = EQUITY_BOOST_BALANCE;
   result["EQUITY_BOOST_TOP_PERCENT"] = EQUITY_BOOST_TOP_PERCENT;
   result["POWER_BOOST_TOP_PERCENT"] = POWER_BOOST_TOP_PERCENT;
   result["EQUITY_INTERVAL"] = EQUITY_INTERVAL;
   result["EQUITY_INTERVAL_BLOCKS"] = EQUITY_INTERVAL_BLOCKS;
   result["DIVIDEND_SHARE_PERCENT"] = DIVIDEND_SHARE_PERCENT;
   result["LIQUID_DIVIDEND_PERCENT"] = LIQUID_DIVIDEND_PERCENT;
   result["STAKED_DIVIDEND_PERCENT"] = STAKED_DIVIDEND_PERCENT;
   result["SAVINGS_DIVIDEND_PERCENT"] = SAVINGS_DIVIDEND_PERCENT;
   result["BOND_COLLATERALIZATION_PERCENT"] = BOND_COLLATERALIZATION_PERCENT;
   result["BOND_COUPON_RATE_PERCENT"] = BOND_COUPON_RATE_PERCENT;
   result["BOND_COUPON_INTERVAL"] = BOND_COUPON_INTERVAL;
   result["BOND_COUPON_INTERVAL_BLOCKS"] = BOND_COUPON_INTERVAL_BLOCKS;
   result["CREDIT_BUYBACK_INTERVAL"] = CREDIT_BUYBACK_INTERVAL;
   result["CREDIT_BUYBACK_INTERVAL_BLOCKS"] = CREDIT_BUYBACK_INTERVAL_BLOCKS;
   result["BUYBACK_SHARE_PERCENT"] = BUYBACK_SHARE_PERCENT;
   result["LIQUID_FIXED_INTEREST_RATE"] = LIQUID_FIXED_INTEREST_RATE;
   result["LIQUID_VARIABLE_INTEREST_RATE"] = LIQUID_VARIABLE_INTEREST_RATE;
   result["STAKED_FIXED_INTEREST_RATE"] = STAKED_FIXED_INTEREST_RATE;
   result["STAKED_VARIABLE_INTEREST_RATE"] = STAKED_VARIABLE_INTEREST_RATE;
   result["SAVINGS_FIXED_INTEREST_RATE"] = SAVINGS_FIXED_INTEREST_RATE;
   result["SAVINGS_VARIABLE_INTEREST_RATE"] = SAVINGS_VARIABLE_INTEREST_RATE;
   result["VAR_INTEREST_RANGE"] = VAR_INTEREST_RANGE;

   result["AUCTION_INTERVAL"] = AUCTION_INTERVAL;
   result["AUCTION_INTERVAL_BLOCKS"] = AUCTION_INTERVAL_BLOCKS;
   result["OPTION_INTERVAL"] = OPTION_INTERVAL;
   result["OPTION_INTERVAL_BLOCKS"] = OPTION_INTERVAL_BLOCKS;
   result["OPTION_NUM_STRIKES"] = OPTION_NUM_STRIKES;
   result["OPTION_STRIKE_WIDTH_PERCENT"] = OPTION_STRIKE_WIDTH_PERCENT;
   result["OPTION_ASSET_MULTIPLE"] = OPTION_ASSET_MULTIPLE;
   result["OPTION_SIG_FIGURES"] = OPTION_SIG_FIGURES;
   result["STIMULUS_INTERVAL"] = STIMULUS_INTERVAL;
   result["STIMULUS_INTERVAL_BLOCKS"] = STIMULUS_INTERVAL_BLOCKS;
   result["DISTRIBUTION_INTERVAL"] = DISTRIBUTION_INTERVAL;
   result["DISTRIBUTION_INTERVAL_BLOCKS"] = DISTRIBUTION_INTERVAL_BLOCKS;
   result["UNIQUE_INTERVAL"] = UNIQUE_INTERVAL;
   result["UNIQUE_INTERVAL_BLOCKS"] = UNIQUE_INTERVAL_BLOCKS;

   result["ASSET_SETTLEMENT_DELAY"] = ASSET_SETTLEMENT_DELAY;
   result["ASSET_SETTLEMENT_OFFSET"] = ASSET_SETTLEMENT_OFFSET;
   result["ASSET_SETTLEMENT_MAX_VOLUME"] = ASSET_SETTLEMENT_MAX_VOLUME;
   result["PRICE_FEED_LIFETIME"] = PRICE_FEED_LIFETIME;
   result["MAX_AUTHORITY_MEMBERSHIP"] = MAX_AUTHORITY_MEMBERSHIP;
   result["MAX_ASSET_WHITELIST_AUTHORITIES"] = MAX_ASSET_WHITELIST_AUTHORITIES;
   result["MAX_ASSET_FEED_PUBLISHERS"] = MAX_ASSET_FEED_PUBLISHERS;
   result["MAX_URL_LENGTH"] = MAX_URL_LENGTH;
   result["MIN_TRANSACTION_SIZE_LIMIT"] = MIN_TRANSACTION_SIZE_LIMIT;
   result["MAX_TRANSACTION_SIZE"] = MAX_TRANSACTION_SIZE;
   result["MIN_BLOCK_SIZE_LIMIT"] = MIN_BLOCK_SIZE_LIMIT;
   result["MAX_BLOCK_SIZE"] = MAX_BLOCK_SIZE;
   result["MIN_BLOCK_SIZE"] = MIN_BLOCK_SIZE;
   result["MIN_FEED_LIFETIME"] = MIN_FEED_LIFETIME;
   result["MIN_SETTLEMENT_DELAY"] = MIN_SETTLEMENT_DELAY;
   result["CONNECTION_REQUEST_DURATION"] = CONNECTION_REQUEST_DURATION;
   result["TRANSFER_REQUEST_DURATION"] = TRANSFER_REQUEST_DURATION;
   result["RESET_ACCOUNT_DELAY"] = RESET_ACCOUNT_DELAY;
   result["NETWORK_UPDATE_INTERVAL"] = NETWORK_UPDATE_INTERVAL;
   result["NETWORK_UPDATE_INTERVAL_BLOCKS"] = NETWORK_UPDATE_INTERVAL_BLOCKS;
   result["MEDIAN_LIQUIDITY_INTERVAL"] = MEDIAN_LIQUIDITY_INTERVAL;
   result["MEDIAN_LIQUIDITY_INTERVAL_BLOCKS"] = MEDIAN_LIQUIDITY_INTERVAL_BLOCKS;
   result["REP_UPDATE_BLOCK_INTERVAL"] = REP_UPDATE_BLOCK_INTERVAL;
   result["METRIC_INTERVAL"] = METRIC_INTERVAL;
   result["METRIC_INTERVAL_BLOCKS"] = METRIC_INTERVAL_BLOCKS;
   result["METRIC_CALC_TIME"] = METRIC_CALC_TIME;
   result["MESSAGE_COUNT_INTERVAL"] = MESSAGE_COUNT_INTERVAL;
   result["MESSAGE_COUNT_INTERVAL_BLOCKS"] = MESSAGE_COUNT_INTERVAL_BLOCKS;
   result["FEED_INTERVAL_BLOCKS"] = FEED_INTERVAL_BLOCKS;
   result["FEED_HISTORY_WINDOW"] = FEED_HISTORY_WINDOW;
   result["MAX_FEED_AGE"] = MAX_FEED_AGE;
   result["MIN_FEEDS"] = MIN_FEEDS;
   result["MIN_UNDO_HISTORY"] = MIN_UNDO_HISTORY;
   result["MAX_UNDO_HISTORY"] = MAX_UNDO_HISTORY;
   result["MAX_INSTANCE_ID"] = MAX_INSTANCE_ID;
   result["VIRTUAL_SCHEDULE_LAP_LENGTH"] = VIRTUAL_SCHEDULE_LAP_LENGTH;
   result["INVALID_OUTCOME_SYMBOL"] = INVALID_OUTCOME_SYMBOL;
   result["INVALID_OUTCOME_DETAILS"] = INVALID_OUTCOME_DETAILS;

   result["GENESIS_ACCOUNT_BASE_NAME"] = GENESIS_ACCOUNT_BASE_NAME;
   result["PRODUCER_ACCOUNT"] = PRODUCER_ACCOUNT;
   result["NULL_ACCOUNT"] = NULL_ACCOUNT;
   result["TEMP_ACCOUNT"] = TEMP_ACCOUNT;
   result["PROXY_TO_SELF_ACCOUNT"] = PROXY_TO_SELF_ACCOUNT;
   result["ROOT_POST_PARENT"] = ROOT_POST_PARENT;
   result["ANON_ACCOUNT"] = ANON_ACCOUNT;
   result["ANON_ACCOUNT_PASSWORD"] = ANON_ACCOUNT_PASSWORD;
   result["ASSET_UNIT_SENDER"] = ASSET_UNIT_SENDER;

   result["INIT_ACCOUNT"] = INIT_ACCOUNT;
   result["INIT_ACCOUNT_PASSWORD"] = INIT_ACCOUNT_PASSWORD;
   result["INIT_DETAILS"] = INIT_DETAILS;
   result["INIT_URL"] = INIT_URL;
   result["INIT_NODE_ENDPOINT"] = INIT_NODE_ENDPOINT;
   result["INIT_AUTH_ENDPOINT"] = INIT_AUTH_ENDPOINT;
   result["INIT_NOTIFICATION_ENDPOINT"] = INIT_NOTIFICATION_ENDPOINT;
   result["INIT_IPFS_ENDPOINT"] = INIT_IPFS_ENDPOINT;
   result["INIT_BITTORRENT_ENDPOINT"] = INIT_BITTORRENT_ENDPOINT;
   result["INIT_IMAGE"] = INIT_IMAGE;
   result["INIT_CEO"] = INIT_CEO;
   result["INIT_COMMUNITY"] = INIT_COMMUNITY;

   return result;
}

} } // node::protocol
