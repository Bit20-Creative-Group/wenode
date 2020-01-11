
#include <node/chain/asset_object.hpp>
#include <node/chain/node_object_types.hpp>
#include <node/chain/database.hpp>

#include <fc/io/raw.hpp>
#include <fc/uint128.hpp>

namespace node { namespace chain {

void asset_bitasset_data_object::update_median_feeds( time_point current_time )
{
   current_feed_publication_time = current_time;
   vector<std::reference_wrapper<const price_feed>> current_feeds;

   // find feeds that were alive at current_time
   for( const pair<account_name_type, pair<time_point,price_feed>>& f : feeds )
   {
      if( (current_time - f.second.first).to_seconds() < options.feed_lifetime_sec &&
          f.second.first != time_point() )
      {
         current_feeds.emplace_back(f.second.second);
         current_feed_publication_time = std::min(current_feed_publication_time, f.second.first);
      }
   }

   // If there are no valid feeds, or the number available is less than the minimum to calculate a median...
   if( current_feeds.size() < options.minimum_feeds )
   {
      //... don't calculate a median, and set a null feed
      feed_cer_updated = false; // new median cer is null, won't update asset_object anyway, set to false for better performance
      current_feed_publication_time = current_time;
      current_feed = price_feed();
      current_maintenance_collateralization = price();

      return;
   }
   if( current_feeds.size() == 1 )
   {
      if( current_feed.core_exchange_rate != current_feeds.front().get().core_exchange_rate )
         feed_cer_updated = true;
      current_feed = current_feeds.front();
      // Note: perhaps can defer updating current_maintenance_collateralization for better performance
      current_maintenance_collateralization = current_feed.maintenance_collateralization();
      return;
   }

   // *** Begin Median Calculations ***
   price_feed median_feed;
   const auto median_itr = current_feeds.begin() + current_feeds.size() / 2;
#define CALCULATE_MEDIAN_VALUE(r, data, field_name) \
   std::nth_element( current_feeds.begin(), median_itr, current_feeds.end(), \
                     [](const price_feed& a, const price_feed& b) { \
      return a.field_name < b.field_name; \
   }); \
   median_feed.field_name = median_itr->get().field_name;

   BOOST_PP_SEQ_FOR_EACH( CALCULATE_MEDIAN_VALUE, ~, GRAPHENE_PRICE_FEED_FIELDS )
#undef CALCULATE_MEDIAN_VALUE
   // *** End Median Calculations ***

   if( current_feed.core_exchange_rate != median_feed.core_exchange_rate )
      feed_cer_updated = true;
   current_feed = median_feed;
   // Note: perhaps can defer updating current_maintenance_collateralization for better performance
   current_maintenance_collateralization = current_feed.maintenance_collateralization();
};

}} //node::chain