#include <node/protocol/config.hpp>
#include <node/protocol/types.hpp>

#include <fc/crypto/base58.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/exception/exception.hpp>
#include <fc/io/raw.hpp>

namespace node { namespace protocol {

   public_key_type::public_key_type():key_data(){};

   public_key_type::public_key_type( const fc::ecc::public_key_data& data )
      :key_data( data ) {};

   public_key_type::public_key_type( const fc::ecc::public_key& pubkey )
      :key_data( pubkey ) {};

   public_key_type::public_key_type( const std::string& base58str )
   {
      std::string prefix( ADDRESS_PREFIX );

      const size_t prefix_len = prefix.size();
      FC_ASSERT( base58str.size() > prefix_len );
      FC_ASSERT( base58str.substr( 0, prefix_len ) ==  prefix , "", ("base58str", base58str) );
      auto bin = fc::from_base58( base58str.substr( prefix_len ) );
      auto bin_key = fc::raw::unpack<binary_key>(bin);
      key_data = bin_key.data;
      FC_ASSERT( fc::ripemd160::hash( key_data.data, key_data.size() )._hash[0] == bin_key.check );
   };


   public_key_type::operator fc::ecc::public_key_data() const
   {
      return key_data;
   };

   public_key_type::operator fc::ecc::public_key() const
   {
      return fc::ecc::public_key( key_data );
   };

   public_key_type::operator std::string() const
   {
      binary_key k;
      k.data = key_data;
      k.check = fc::ripemd160::hash( k.data.data, k.data.size() )._hash[0];
      auto data = fc::raw::pack( k );
      return ADDRESS_PREFIX + fc::to_base58( data.data(), data.size() );
   }

   bool operator == ( const public_key_type& p1, const fc::ecc::public_key& p2)
   {
      return p1.key_data == p2.serialize();
   }

   bool operator == ( const public_key_type& p1, const public_key_type& p2)
   {
      return p1.key_data == p2.key_data;
   }

   bool operator != ( const public_key_type& p1, const public_key_type& p2)
   {
      return p1.key_data != p2.key_data;
   }

   // extended_public_key_type

   extended_public_key_type::extended_public_key_type():key_data(){};

   extended_public_key_type::extended_public_key_type( const fc::ecc::extended_key_data& data )
      :key_data( data ){};

   extended_public_key_type::extended_public_key_type( const fc::ecc::extended_public_key& extpubkey )
   {
      key_data = extpubkey.serialize_extended();
   };

   extended_public_key_type::extended_public_key_type( const std::string& base58str )
   {
      std::string prefix( ADDRESS_PREFIX );

      const size_t prefix_len = prefix.size();
      FC_ASSERT( base58str.size() > prefix_len );
      FC_ASSERT( base58str.substr( 0, prefix_len ) ==  prefix , "", ("base58str", base58str) );
      auto bin = fc::from_base58( base58str.substr( prefix_len ) );
      auto bin_key = fc::raw::unpack<binary_key>(bin);
      FC_ASSERT( fc::ripemd160::hash( bin_key.data.data, bin_key.data.size() )._hash[0] == bin_key.check );
      key_data = bin_key.data;
   }

   extended_public_key_type::operator fc::ecc::extended_public_key() const
   {
      return fc::ecc::extended_public_key::deserialize( key_data );
   }

   extended_public_key_type::operator std::string() const
   {
      binary_key k;
      k.data = key_data;
      k.check = fc::ripemd160::hash( k.data.data, k.data.size() )._hash[0];
      auto data = fc::raw::pack( k );
      return ADDRESS_PREFIX + fc::to_base58( data.data(), data.size() );
   }

   bool operator == ( const extended_public_key_type& p1, const fc::ecc::extended_public_key& p2)
   {
      return p1.key_data == p2.serialize_extended();
   }

   bool operator == ( const extended_public_key_type& p1, const extended_public_key_type& p2)
   {
      return p1.key_data == p2.key_data;
   }

   bool operator != ( const extended_public_key_type& p1, const extended_public_key_type& p2)
   {
      return p1.key_data != p2.key_data;
   }

   // extended_private_key_type

   extended_private_key_type::extended_private_key_type():key_data(){};

   extended_private_key_type::extended_private_key_type( const fc::ecc::extended_key_data& data )
      :key_data( data ){};

   extended_private_key_type::extended_private_key_type( const fc::ecc::extended_private_key& extprivkey )
   {
      key_data = extprivkey.serialize_extended();
   };

   extended_private_key_type::extended_private_key_type( const std::string& base58str )
   {
      std::string prefix( ADDRESS_PREFIX );

      const size_t prefix_len = prefix.size();
      FC_ASSERT( base58str.size() > prefix_len );
      FC_ASSERT( base58str.substr( 0, prefix_len ) ==  prefix , "", ("base58str", base58str) );
      auto bin = fc::from_base58( base58str.substr( prefix_len ) );
      auto bin_key = fc::raw::unpack<binary_key>(bin);
      FC_ASSERT( fc::ripemd160::hash( bin_key.data.data, bin_key.data.size() )._hash[0] == bin_key.check );
      key_data = bin_key.data;
   }

   extended_private_key_type::operator fc::ecc::extended_private_key() const
   {
      return fc::ecc::extended_private_key::deserialize( key_data );
   }

   extended_private_key_type::operator std::string() const
   {
      binary_key k;
      k.data = key_data;
      k.check = fc::ripemd160::hash( k.data.data, k.data.size() )._hash[0];
      auto data = fc::raw::pack( k );
      return ADDRESS_PREFIX + fc::to_base58( data.data(), data.size() );
   }

   bool operator == ( const extended_private_key_type& p1, const fc::ecc::extended_public_key& p2)
   {
      return p1.key_data == p2.serialize_extended();
   }

   bool operator == ( const extended_private_key_type& p1, const extended_private_key_type& p2)
   {
      return p1.key_data == p2.key_data;
   }

   bool operator != ( const extended_private_key_type& p1, const extended_private_key_type& p2)
   {
      return p1.key_data != p2.key_data;
   }

   // Encrypted Keypair Type

   bool operator == ( const encrypted_keypair_type& p1, const encrypted_keypair_type& p2 )
   {
      return std::tie( p1.secure_key, p1.public_key, p1.encrypted_private_key ) == std::tie( p2.secure_key, p2.public_key, p2.encrypted_private_key );
   }

   bool operator < ( const encrypted_keypair_type& p1, const encrypted_keypair_type& p2 )
   {
      return std::tie( p1.secure_key, p1.public_key, p1.encrypted_private_key ) < std::tie( p2.secure_key, p2.public_key, p2.encrypted_private_key );
   }

   bool operator > ( const encrypted_keypair_type& p1, const encrypted_keypair_type& p2 )
   {
      return std::tie( p1.secure_key, p1.public_key, p1.encrypted_private_key ) > std::tie( p2.secure_key, p2.public_key, p2.encrypted_private_key );
   }

   bool operator != ( const encrypted_keypair_type& p1, const encrypted_keypair_type& p2 )
   {
      return std::tie( p1.secure_key, p1.public_key, p1.encrypted_private_key ) != std::tie( p2.secure_key, p2.public_key, p2.encrypted_private_key );
   }

   bool operator <= ( const encrypted_keypair_type& p1, const encrypted_keypair_type& p2 )
   {
      return std::tie( p1.secure_key, p1.public_key, p1.encrypted_private_key ) <= std::tie( p2.secure_key, p2.public_key, p2.encrypted_private_key );
   }
   
   bool operator >= ( const encrypted_keypair_type& p1, const encrypted_keypair_type& p2 )
   {
      return std::tie( p1.secure_key, p1.public_key, p1.encrypted_private_key ) >= std::tie( p2.secure_key, p2.public_key, p2.encrypted_private_key );
   }

} } // node::protocol

namespace fc
{
   using namespace std;
   void to_variant( const node::protocol::public_key_type& var,  fc::variant& vo )
   {
      vo = std::string( var );
   }

   void from_variant( const fc::variant& var,  node::protocol::public_key_type& vo )
   {
      vo = node::protocol::public_key_type( var.as_string() );
   }

   void to_variant( const node::protocol::extended_public_key_type& var, fc::variant& vo )
   {
      vo = std::string( var );
   }

   void from_variant( const fc::variant& var, node::protocol::extended_public_key_type& vo )
   {
      vo = node::protocol::extended_public_key_type( var.as_string() );
   }

   void to_variant( const node::protocol::extended_private_key_type& var, fc::variant& vo )
   {
      vo = std::string( var );
   }

   void from_variant( const fc::variant& var, node::protocol::extended_private_key_type& vo )
   {
      vo = node::protocol::extended_private_key_type( var.as_string() );
   }
} // fc
