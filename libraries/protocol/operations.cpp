#include <node/protocol/operations.hpp>

#include <node/protocol/operation_util_impl.hpp>

namespace node { namespace protocol {

struct is_market_op_visitor {
   typedef bool result_type;

   template<typename T>
   bool operator()( T&& v )const { return false; }
   bool operator()( const limit_order_create_operation& )const { return true; }
   bool operator()( const limit_order_cancel_operation& )const { return true; }
   bool operator()( const transfer_operation& )const { return true; }
   bool operator()( const transferTMEtoSCOREfund_operation& )const { return true; }
};

bool is_market_operation( const operation& op ) {
   return op.visit( is_market_op_visitor() );
}

struct is_vop_visitor
{
   typedef bool result_type;

   template< typename T >
   bool operator()( const T& v )const { return v.is_virtual(); }
};

bool is_virtual_operation( const operation& op )
{
   return op.visit( is_vop_visitor() );
}

} } // node::protocol

DEFINE_OPERATION_TYPE( node::protocol::operation )
