#include <node/protocol/get_config.hpp>
#include <node/protocol/config.hpp>
#include <node/protocol/asset.hpp>
#include <node/protocol/types.hpp>
#include <node/protocol/version.hpp>

namespace node { namespace protocol {

fc::variant_object get_config()
{
   fc::mutable_variant_object result;

#ifdef IS_TEST_NET
   result[ "IS_TEST_NET" ] = true;
#else
   result[ "IS_TEST_NET" ] = false;
#endif

   result["SHOW_PRIVATE_KEYS"] = SHOW_PRIVATE_KEYS;
   result["GEN_PRIVATE_KEY"] = GEN_PRIVATE_KEY;
   result["SYMBOL_USD"] = SYMBOL_USD;
   result["PERCENT_100"] = PERCENT_100;
   result["PERCENT_1"] = PERCENT_1;
   result["PERCENT_10_OF_PERCENT_1"] = PERCENT_10_OF_PERCENT_1;
   result["ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD"] = ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD;
   result["ACTIVE_CHALLENGE_COOLDOWN"] = ACTIVE_CHALLENGE_COOLDOWN;
   result["ACTIVE_CHALLENGE_FEE"] = ACTIVE_CHALLENGE_FEE;
   result["ADDRESS_PREFIX"] = ADDRESS_PREFIX;
   result["BANDWIDTH_AVERAGE_WINDOW_MICROSECONDS"] = BANDWIDTH_AVERAGE_WINDOW_MICROSECONDS;
   result["BANDWIDTH_PRECISION"] = BANDWIDTH_PRECISION;
   result["BLOCKCHAIN_PRECISION"] = BLOCKCHAIN_PRECISION;
   result["BLOCKCHAIN_PRECISION_DIGITS"] = BLOCKCHAIN_PRECISION_DIGITS;
   result["BLOCKCHAIN_HARDFORK_VERSION"] = BLOCKCHAIN_HARDFORK_VERSION;
   result["BLOCKCHAIN_VERSION"] = BLOCKCHAIN_VERSION;
   result["BLOCK_INTERVAL"] = BLOCK_INTERVAL;
   result["BLOCKS_PER_DAY"] = BLOCKS_PER_DAY;
   result["BLOCKS_PER_HOUR"] = BLOCKS_PER_HOUR;
   result["BLOCKS_PER_YEAR"] = BLOCKS_PER_YEAR;
   result["CASHOUT_WINDOW"] = CASHOUT_WINDOW;
   result["CHAIN_ID"] = CHAIN_ID;
   result["CONTENT_CONSTANT"] = CONTENT_CONSTANT;
   result["CONTENT_REWARD_PERCENT"] = CONTENT_REWARD_PERCENT;
   result["CREATE_ACCOUNT_DELEGATION_RATIO"] = CREATE_ACCOUNT_DELEGATION_RATIO;
   result["CREATE_ACCOUNT_DELEGATION_TIME"] = CREATE_ACCOUNT_DELEGATION_TIME;
   result["CREDIT_INTEREST_RATE"] = CREDIT_INTEREST_RATE;
   result["EQUIHASH_K"] = EQUIHASH_K;
   result["EQUIHASH_N"] = EQUIHASH_N;
   result["FEED_HISTORY_WINDOW"] = FEED_HISTORY_WINDOW;
   result["FEED_INTERVAL_BLOCKS"] = FEED_INTERVAL_BLOCKS;
   result["METRIC_INTERVAL_BLOCKS"] = METRIC_INTERVAL_BLOCKS;
   result["FREE_TRANSACTIONS_WITH_NEW_ACCOUNT"] = FREE_TRANSACTIONS_WITH_NEW_ACCOUNT;
   result["GENESIS_TIME"] = GENESIS_TIME;
   result["HARDFORK_REQUIRED_WITNESSES"] = HARDFORK_REQUIRED_WITNESSES;
   result["GENESIS_ACCOUNT_BASE_NAME"] = GENESIS_ACCOUNT_BASE_NAME;
   result["INIT_PUBLIC_KEY_STR"] = INIT_PUBLIC_KEY_STR;
	result["GENESIS_ACCOUNT_COIN_STAKE"] = GENESIS_ACCOUNT_COIN_STAKE;
#ifdef SHOW_PRIVATE_KEYS
   #if SHOW_PRIVATE_KEYS
      #ifdef INIT_PRIVATE_KEY
        result["INIT_PRIVATE_KEY"] = INIT_PRIVATE_KEY;
      #endif
   #endif
   // do not expose private key, period. - from steem devs
   // we need this line present but inactivated so CI check for all constants in config.hpp doesn't complain.
#endif
   result["INIT_COIN_SUPPLY"] = INIT_COIN_SUPPLY;
   result["INIT_TIME"] = INIT_TIME;
   result["IRREVERSIBLE_THRESHOLD"] = IRREVERSIBLE_THRESHOLD;
   result["MAX_ACCOUNT_NAME_LENGTH"] = MAX_ACCOUNT_NAME_LENGTH;
   result["MAX_ACC_WITNESS_VOTES"] = MAX_ACC_WITNESS_VOTES;
   result["MAX_ASSET_WHITELIST_AUTHORITIES"] = MAX_ASSET_WHITELIST_AUTHORITIES;
   result["MAX_AUTHORITY_MEMBERSHIP"] = MAX_AUTHORITY_MEMBERSHIP;
   result["MAX_BLOCK_SIZE"] = MAX_BLOCK_SIZE;
   result["MAX_COMMENT_DEPTH"] = MAX_COMMENT_DEPTH;
   result["MAX_COMMENT_DEPTH_PRE_HF17"] = MAX_COMMENT_DEPTH_PRE_HF17;
   result["MAX_FEED_AGE"] = MAX_FEED_AGE;
   result["MAX_INSTANCE_ID"] = MAX_INSTANCE_ID;
   result["MAX_MEMO_SIZE"] = MAX_MEMO_SIZE;
   result["TOTAL_PRODUCERS"] = TOTAL_PRODUCERS;
   result["MAX_PERMLINK_LENGTH"] = MAX_PERMLINK_LENGTH;
   result["MAX_PROXY_RECURSION_DEPTH"] = MAX_PROXY_RECURSION_DEPTH;
   result["MAX_RATION_DECAY_RATE"] = MAX_RATION_DECAY_RATE;
   result["MAX_RESERVE_RATIO"] = MAX_RESERVE_RATIO;
   result["MAX_SIG_CHECK_DEPTH"] = MAX_SIG_CHECK_DEPTH;
   result["MAX_TIME_UNTIL_EXPIRATION"] = MAX_TIME_UNTIL_EXPIRATION;
   result["MAX_TRANSACTION_SIZE"] = MAX_TRANSACTION_SIZE;
   result["MAX_UNDO_HISTORY"] = MAX_UNDO_HISTORY;
   result["MAX_URL_LENGTH"] = MAX_URL_LENGTH;
   result["MAX_VOTE_CHANGES"] = MAX_VOTE_CHANGES;
   result["MAX_WITHDRAW_ROUTES"] = MAX_WITHDRAW_ROUTES;
   result["MAX_STRING_LENGTH"] = MAX_STRING_LENGTH;
   result["MIN_ACCOUNT_CREATION_FEE"] = MIN_ACCOUNT_CREATION_FEE;
   result["MIN_ACCOUNT_NAME_LENGTH"] = MIN_ACCOUNT_NAME_LENGTH;
   result["MIN_BLOCK_SIZE"] = MIN_BLOCK_SIZE;
   result["MIN_BLOCK_SIZE_LIMIT"] = MIN_BLOCK_SIZE_LIMIT;
   result["MIN_CONTENT_REWARD"] = MIN_CONTENT_REWARD;
   result["MIN_CURATE_REWARD"] = MIN_CURATE_REWARD;
   result["MIN_PERMLINK_LENGTH"] = MIN_PERMLINK_LENGTH;
   result["MIN_REPLY_INTERVAL"] = MIN_REPLY_INTERVAL;
   result["MIN_ROOT_COMMENT_INTERVAL"] = MIN_ROOT_COMMENT_INTERVAL;
   result["MIN_VOTE_INTERVAL_SEC"] = MIN_VOTE_INTERVAL_SEC;
   result["WITNESS_ACCOUNT"] = WITNESS_ACCOUNT;
   result["MINER_PAY_PERCENT"] = MINER_PAY_PERCENT;
   result["MIN_FEEDS"] = MIN_FEEDS;
   result["MINING_REWARD"] = MINING_REWARD;
   result["MINING_TIME"] = MINING_TIME;
   result["MIN_PAYOUT_USD"] = MIN_PAYOUT_USD;
   result["MIN_POW_REWARD"] = MIN_POW_REWARD;
   result["MIN_PRODUCER_REWARD"] = MIN_PRODUCER_REWARD;
   result["MIN_RATION"] = MIN_RATION;
   result["MIN_TRANSACTION_SIZE_LIMIT"] = MIN_TRANSACTION_SIZE_LIMIT;
   result["MIN_UNDO_HISTORY"] = MIN_UNDO_HISTORY;
   result["NULL_ACCOUNT"] = NULL_ACCOUNT;
   result["GENESIS_WITNESS_AMOUNT"] = GENESIS_WITNESS_AMOUNT;
   result["GENESIS_EXTRA_WITNESSES"] = GENESIS_EXTRA_WITNESSES;
   result["OWNER_AUTH_HISTORY_TRACKING_START_BLOCK_NUM"] = OWNER_AUTH_HISTORY_TRACKING_START_BLOCK_NUM;
   result["OWNER_AUTH_RECOVERY_PERIOD"] = OWNER_AUTH_RECOVERY_PERIOD;
   result["OWNER_CHALLENGE_COOLDOWN"] = OWNER_CHALLENGE_COOLDOWN;
   result["OWNER_CHALLENGE_FEE"] = OWNER_CHALLENGE_FEE;
   result["OWNER_UPDATE_LIMIT"] = OWNER_UPDATE_LIMIT;
   result["POST_AVERAGE_WINDOW"] = POST_AVERAGE_WINDOW;
   result["POST_MAX_BANDWIDTH"] = POST_MAX_BANDWIDTH;
   result["POST_WEIGHT_CONSTANT"] = POST_WEIGHT_CONSTANT;
   result["PROXY_TO_SELF_ACCOUNT"] = PROXY_TO_SELF_ACCOUNT;
   result["INTEREST_COMPOUND_INTERVAL"] = INTEREST_COMPOUND_INTERVAL;
   result["SECONDS_PER_YEAR"] = SECONDS_PER_YEAR;
   result["RECENT_REWARD_DECAY_RATE"] = RECENT_REWARD_DECAY_RATE;
   result["REVERSE_AUCTION_WINDOW_SECONDS"] = REVERSE_AUCTION_WINDOW_SECONDS;
   result["ROOT_POST_PARENT"] = ROOT_POST_PARENT;
   result["SAVINGS_WITHDRAW_REQUEST_LIMIT"] = SAVINGS_WITHDRAW_REQUEST_LIMIT;
   result["SAVINGS_WITHDRAW_TIME"] = SAVINGS_WITHDRAW_TIME;
   result["SOFT_MAX_COMMENT_DEPTH"] = SOFT_MAX_COMMENT_DEPTH;
   result["TEMP_ACCOUNT"] = TEMP_ACCOUNT;
   result["UPVOTE_LOCKOUT_TIME"] = UPVOTE_LOCKOUT_TIME;
   result["COIN_UNSTAKE_INTERVALS"] = COIN_UNSTAKE_INTERVALS;
   result["STAKE_WITHDRAW_INTERVAL_SECONDS"] = STAKE_WITHDRAW_INTERVAL_SECONDS;
   result["VOTE_CHANGE_LOCKOUT_PERIOD"] = VOTE_CHANGE_LOCKOUT_PERIOD;
   result["VOTE_DUST_THRESHOLD"] = VOTE_DUST_THRESHOLD;
   result["VOTE_REGENERATION_SECONDS"] = VOTE_REGENERATION_SECONDS;
   result["SYMBOL_COIN"] = SYMBOL_COIN;
   result["VIRTUAL_SCHEDULE_LAP_LENGTH"] = VIRTUAL_SCHEDULE_LAP_LENGTH;

   return result;
}

} } // node::protocol
