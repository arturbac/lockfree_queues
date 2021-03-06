// MIT License
// 
// Copyright (c) 2019 Artur Bac
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Non-Blocking Concurrent Queue Algorithms, lock free

#pragma once

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <unistd.h>
#include <atomic>

namespace ampi
{
  inline void sleep( uint32_t ms ) { usleep(ms*1000); }
  
  enum struct memorder : int { relaxed = __ATOMIC_RELAXED, acquire = __ATOMIC_ACQUIRE,  release = __ATOMIC_RELEASE, acq_rel = __ATOMIC_ACQ_REL };
  

  template<typename T, typename U>
	inline bool atomic_compare_exchange( T * loc [[gnu::nonnull]], U comparand, U value,
                                       memorder success = memorder::release,
                                      memorder fail = memorder::relaxed ) 
    { return __atomic_compare_exchange_n( loc, &comparand, value, false, static_cast<int32_t>(success), static_cast<int32_t>(fail)); }
    
  template<typename type>
  inline type atomic_load( type * ref, memorder order) noexcept
    {
    return type{ __atomic_load_n( ref, static_cast<int>(order) ) };
    }
    
  
  template<typename type>
  inline type atomic_add_fetch(type * ptr, type value, memorder order ) noexcept
    { return __atomic_add_fetch ( ptr, value, static_cast<int>(order)); }
    
  template<typename type>
  inline type atomic_sub_fetch(type * ptr, type value, memorder order ) noexcept
    { return __atomic_sub_fetch ( ptr, value, static_cast<int>(order)); } 
    
  //----------------------------------------------------------------------------------------------------------------------
  //
  // pointer_t
  //
  // used by fifo holds pointer with counters
  //----------------------------------------------------------------------------------------------------------------------
  template<typename NODE_TYPE>
  union pointer_t
  {
  public:
    typedef NODE_TYPE        node_type;
    typedef pointer_t<node_type>  class_type;

    template<typename NODE_T>
    friend bool cas( pointer_t<NODE_T> & dest, pointer_t<NODE_T>  const & compare, pointer_t<NODE_T> const & swap );
  public:

    struct data_t 
      {
      uint64_t ptr_value    : 48;
      uint64_t count        : 16;
      } data;
    int64_t cas_value;

  public:
    pointer_t() noexcept ;
    pointer_t( node_type * n, unsigned c = 0 ) noexcept ;
    pointer_t( pointer_t const & other ) noexcept = default;
    explicit pointer_t( int64_t value ) noexcept { cas_value = value; }
  public:
    inline bool operator ==( class_type const & other ) const;
    class_type & operator =( class_type const & other ) noexcept = default;

  public:
    unsigned count() const { return this->data.count; }
    node_type * get() const noexcept { return reinterpret_cast<node_type*>( this->data.ptr_value ); }
    node_type * operator->() const noexcept { return get(); }
    explicit operator bool() const noexcept { return get() != nullptr; }
    void set_ptr( node_type * value );
    };


  template<typename NODE_TYPE>
  inline bool cas( pointer_t<NODE_TYPE> & dest, pointer_t<NODE_TYPE> const & compare
    , typename pointer_t<NODE_TYPE>::node_type * node, unsigned counter )
    {
    pointer_t<NODE_TYPE> tmp_value( node, counter );
    return  cas( dest, compare, tmp_value );
    }


  template<typename NODE_TYPE>
  inline bool cas( pointer_t<NODE_TYPE> & dest, pointer_t<NODE_TYPE> const & compare
    , pointer_t<NODE_TYPE> const & swap )
    {
    bool result = atomic_compare_exchange( &dest.cas_value, compare.cas_value, swap.cas_value);
    return result;
    }

  template<typename node_type>
  inline pointer_t<node_type> atomic_load( pointer_t<node_type> & ptr, memorder order) noexcept
    {
    return pointer_t<node_type>{ __atomic_load_n( &ptr.cas_value, static_cast<int>(order) ) };  
    }
    
  template<typename NODE_TYPE>
  inline pointer_t<NODE_TYPE>::pointer_t() noexcept 
      : cas_value( 0 )
    {}

  template<typename NODE_TYPE>
  inline pointer_t<NODE_TYPE>::pointer_t( node_type * n, unsigned c ) noexcept 
      : cas_value( 0 )
    {
    this->set_ptr( n );
    this->data.count = static_cast<uint64_t>( c % 0xFFFF );
    }
    
//   template<typename NODE_TYPE>
//   inline pointer_t<NODE_TYPE>::pointer_t( pointer_t const & other )
//       : cas_value( other.cas_value )
//     {}
  
  template<typename NODE_TYPE>
  inline bool pointer_t<NODE_TYPE>::operator ==( class_type const & other ) const
    {
    bool result( this->cas_value == other.cas_value );
    return result;
    }

//   template<typename NODE_TYPE>
//   inline typename pointer_t<NODE_TYPE>::class_type &
//   pointer_t<NODE_TYPE>::operator =( class_type const & other )
//     {
//     this->cas_value = other.cas_value;
//     return *this;
//     }

  template<typename NODE_TYPE>
  inline void pointer_t<NODE_TYPE>::set_ptr( node_type * value )
    {
    assert( (intptr_t(value) & 0xFFFF000000000000llu) == 0 );
    data.ptr_value = reinterpret_cast<intptr_t>(value) & 0xFFFFFFFFFFFFllu;
    }

  //----------------------------------------------------------------------------------------------------------------------
  //
  // node_t
  //
  //----------------------------------------------------------------------------------------------------------------------
  
  template<typename USER_OBJ_TYPE>
  struct lifo_node_t
    {
    typedef USER_OBJ_TYPE                user_obj_type;
    typedef lifo_node_t<user_obj_type>   class_type;
    typedef class_type *                 pointer_type;

    user_obj_type     value;
//     union {
      pointer_type      next;
//       pointer_t<class_type>  next_cas;
//       };

    lifo_node_t() : value(),  next() {}
    lifo_node_t( user_obj_type && data ) : value( std::forward<user_obj_type &&>(data)), next() {}
    };
    
  template<typename USER_OBJ_TYPE>
  using node_t = lifo_node_t<USER_OBJ_TYPE>;
}
