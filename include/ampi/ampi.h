// MIT License
// 
// Copyright (c) [year] [fullname]
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
// fifo algorithm author Michael L. Scott mlscott at acm.org http://www.cs.rochester.edu/research/synchronization/pseudocode/queues.html
// lifo, apop and fifo implementation Artur Bac artur_at_ebasoft.com.pl
// published under MIT Licence
// ver 1.1 01.10.2009
// ver 2.0 09 03.2019
// - fixed data race under pressure when node is freed and still other thread tries to dequeue
//   push readed next when po just deleted node
// - fixed nullptr derefence at D12 (differs from oroginal algo)
// Note from my disscusion with author of algorithm
// > On Mar 9, 2019, at 6:49 AM, Artur Bać <artur@ebasoft.com.pl> wrote:
// > 
// > Hi.
// > Just wanted You to know that presented on You webpage algo has a "Data
// > Race" causing memory use after free.
// > 
// > Thread 1 - Between D2 and D5 Q->Head ..
// > dequeue
// >   D2:      head = Q->Head         // Read Head
// >   D3:      tail = Q->Tail         // Read Tail
// >   D4:      next = head.ptr->next    // Read Head.ptr->next
// >   D5:      if head == Q->Head         // Are head, tail, and next consistent?
// > 
// > Thread 2 - D20 and returned is being freed which causes read form
// > already freed pointer
// > D19:   free(head.ptr)
// > D20:   return TRUE
// > 
// > I have found this with sanitizer from clang during high pressure of
// > enqueue/dequeue
// > Possible solution to this is I thnik to use reusable node's with
// > lifo lock free queue which is simple in implementation.
// > 
// > It will be sufficient that node is not freed and its contents doesn't
// > matter because of D5 validation of Q->head.
//
//
// W dniu 09.03.2019 o 14:59, Michael L. Scott pisze:
// Thanks, Artur.  The code is actually correct, but the web page (which I hadn't looked at in years)
// is unclear: the code requires an allocator that is type-preserving and that never gives memory back to the OS.
// This requirement is clearer in the actual PODC paper.
// With the type-preserving allocator, a freed and reused node never becomes an object of a different type.
// Thus the counted pointer fields are still counted pointers and the consistency checks discover all races safely,
// despite the fact that a thread may sometimes look at a node that has been reused.
// If this strategy is unacceptable in a given real-world context (e.g., if the queue must use a shared, general purpose allocator),
// the code can be augmented easily to use hazard pointers or epoch- or interval-based reclamation.
// 
// - mls
#include "common_utils.h"

namespace ampi
{

  //----------------------------------------------------------------------------------------------------------------------
  //
  // node_t
  //
  //----------------------------------------------------------------------------------------------------------------------
  
//   template<typename USER_OBJ_TYPE>
//   struct node_t
//     {
//     typedef USER_OBJ_TYPE          user_obj_type;
//     typedef node_t<user_obj_type>  class_type;
//     typedef pointer_t<class_type>  pointer_type;
// 
//     user_obj_type     value;
//     pointer_type      next;
// 
//     node_t() : value(),  next() {}
//     node_t( user_obj_type && data ) : value( std::forward<user_obj_type &&>(data)), next() {}
//     };

  
  template<typename USER_OBJ_TYPE>
  struct lifo_node_t
    {
    typedef USER_OBJ_TYPE                user_obj_type;
    typedef lifo_node_t<user_obj_type>   class_type;
    typedef class_type *                 pointer_type;

    user_obj_type     value;
    union {
      pointer_type      next;
      pointer_t<class_type>  next_cas;
      };

    lifo_node_t() : value(),  next() {}
    lifo_node_t( user_obj_type && data ) : value( std::forward<user_obj_type &&>(data)), next() {}
    };
    
  template<typename USER_OBJ_TYPE>
  using node_t = lifo_node_t<USER_OBJ_TYPE>;
    
  //----------------------------------------------------------------------------------------------------------------------
  //
  // lifo_queue_internal_tmpl
  //
  //----------------------------------------------------------------------------------------------------------------------
  ///\brief lifo queue used internaly for node managment
  template<typename USER_OBJ_TYPE>
  class lifo_queue_internal_tmpl
    {
  public:
    using user_obj_type = USER_OBJ_TYPE;
    using node_type = lifo_node_t<user_obj_type>;
    using pointer_type = node_type *;
    using size_type = long;
    
  private:
    pointer_type   head_;
    int32_t        size_;
    bool           finish_wating_;
    
  public:
    inline bool        empty() const noexcept                  { return atomic_load( &head_, memorder::acquire ) == nullptr; }
    inline size_type   size() const noexcept                   { return atomic_load( &size_, memorder::acquire ); }
    inline bool        finish_waiting() const noexcept         { return finish_wating_; }
    inline void        finish_waiting( bool value ) noexcept   { finish_wating_ = value ; }
    
  public:
    lifo_queue_internal_tmpl() : head_{} , size_{}, finish_wating_{} {}
    ~lifo_queue_internal_tmpl(){}
    lifo_queue_internal_tmpl( lifo_queue_internal_tmpl const & ) = delete;
    lifo_queue_internal_tmpl & operator=( lifo_queue_internal_tmpl const & ) = delete;
    
  public:
    ///\brief enqueues supplyied node
    void push( node_type * user_data /*[[gnu::nonnull]]*/ );
    
    ///\brief single try to dequeue element
    ///\description @{
    /// when queue is empty returns imediatly
    /// when queue is not empty it retries infinitie number of times until it succeeds or queue becomes empty
    ///@}
    node_type * pull();
    
    ///\breif waits until pop succeeds with sleeping between retrys
    ///\param sleep_millisec time in miliseconds of sleeping
    node_type * pull_wait( size_t sleep_millisec );
    };

  
  template<typename T>
  void lifo_queue_internal_tmpl<T>::push( node_type * next_node /*[[gnu::nonnull]]*/ )
    {
    assert(next_node != nullptr );
    if( !finish_waiting() )
      {
      //atomic linked list
      bool node_submitted {};
      do
        {
        pointer_type last_head { atomic_load( &head_, memorder::acquire ) };
        next_node->next = last_head;
        node_submitted = atomic_compare_exchange( &head_, last_head, next_node );
        }
      while(!node_submitted);
      atomic_add_fetch(&size_, 1, memorder::release );
      }
    }

  template<typename T>
  typename lifo_queue_internal_tmpl<T>::node_type * 
  lifo_queue_internal_tmpl<T>::pull()
    {
    pointer_type head_to_dequeue{atomic_load( &head_, memorder::acquire)};

    for (;nullptr != head_to_dequeue; //return when nothing left in queue
            head_to_dequeue = atomic_load( &head_, memorder::acquire))                                                 // Keep trying until Dequeue is done
      {
      pointer_type next { head_to_dequeue->next };                             //load next head value
      bool deque_is_done = atomic_compare_exchange( &head_, head_to_dequeue, next );//if swap succeeds new head is estabilished
      if( deque_is_done )
        {
        assert(head_to_dequeue != nullptr);
        atomic_sub_fetch(&size_, 1, memorder::release );
        head_to_dequeue->next = pointer_type{};
        break;
        }
      }
    return head_to_dequeue;
    }
  
  template<typename T>
  typename lifo_queue_internal_tmpl<T>::node_type * 
  lifo_queue_internal_tmpl<T>::pull_wait( size_t sleep_millisec )
    {
    for(;;)
      {
      node_type * res = pull();
      if ( res != nullptr || finish_wating_ )
        return res;

      ampi::sleep( static_cast< uint32_t>( sleep_millisec ));
      }
    }
  //----------------------------------------------------------------------------------------------------------------------
  //
  // reuse_node_queue_t
  //
  //----------------------------------------------------------------------------------------------------------------------
  
  template<typename USER_OBJ_TYPE>
  class reuse_node_queue_t 
      : public lifo_queue_internal_tmpl<USER_OBJ_TYPE>
    {
    public:
      typedef USER_OBJ_TYPE user_obj_type;
      typedef lifo_queue_internal_tmpl<USER_OBJ_TYPE> base_type;
      using node_type = typename base_type::node_type;
      
  public:
    ~reuse_node_queue_t() { free_reusable_nodes(); }
    void free_reusable_nodes();
    
    std::unique_ptr<node_type> construct_node( user_obj_type && user_data );
    void reuse_node( std::unique_ptr<node_type> && to_reuse );
    };
    
  template<typename T>
  std::unique_ptr<typename reuse_node_queue_t<T>::node_type>
  reuse_node_queue_t<T>::construct_node( user_obj_type && user_data )
    {
    std::unique_ptr<node_type> result;
    if( ! this->empty() )
      {
      result.reset( this->pull() );
      if(result)
        {
        result->value = std::forward<user_obj_type &&>(user_data);
        using pointer = node_type *;
        result->next = pointer{};
        }
      }
      
    if( ! result )
      result = std::make_unique<node_type>(std::forward<user_obj_type &&>(user_data));
    return result;
    }
    
  template<typename T>
  void reuse_node_queue_t<T>::free_reusable_nodes()
    {
    while( !this->empty() )
      {
      std::unique_ptr<node_type> node_to_free{ this->pull() };
      }
    }
  
  template<typename T>
  void reuse_node_queue_t<T>::reuse_node( std::unique_ptr<node_type> && to_reuse )
    {
    this->push( to_reuse.get() );
    to_reuse.release();
    }
    
  //----------------------------------------------------------------------------------------------------------------------
  //
  // lifo_queue_t
  //
  //----------------------------------------------------------------------------------------------------------------------
  
  template<typename USER_OBJ_TYPE>
  class lifo_queue_t 
      : public lifo_queue_internal_tmpl<USER_OBJ_TYPE>
    {
  public:
    using user_obj_type =  USER_OBJ_TYPE;
    using base_type = lifo_queue_internal_tmpl<user_obj_type>;
    using reuse_node_queue_type = reuse_node_queue_t<user_obj_type>;
    using node_type = typename base_type::node_type;

  private:
    reuse_node_queue_type free_node_to_reuse_;
    
  public:
    lifo_queue_t() : base_type(), free_node_to_reuse_() {}
    lifo_queue_t( lifo_queue_t const & ) = delete;
    lifo_queue_t & operator=( lifo_queue_t const & ) = delete;
    
    void push( user_obj_type && user_data );
    std::pair<user_obj_type, bool> pull();
    };
  
  template<typename T>
  void lifo_queue_t<T>::push( user_obj_type && user_data )
    {
    std::unique_ptr<node_type> next_node { free_node_to_reuse_.construct_node(std::forward<user_obj_type &&>(user_data)) };
    base_type::push( next_node.get() );
    next_node.release();
    }
    
  template<typename T>
  std::pair<typename lifo_queue_t<T>::user_obj_type, bool>  
  lifo_queue_t<T>::pull()
    {
    std::unique_ptr<node_type> detached_node { base_type::pull() };
    
    if( nullptr != detached_node )
      {
      std::pair<user_obj_type, bool> result { std::move( detached_node->value ),  true };
      free_node_to_reuse_.reuse_node( std::move(detached_node) );
      return result;
      }
    return {};
    }

  //----------------------------------------------------------------------------------------------------------------------
  //
  // afifo_queue_result_iterator_tmpl
  //
  //----------------------------------------------------------------------------------------------------------------------
  template<typename USER_OBJ_TYPE>
  class afifo_queue_internal_tmpl;
  
  template<typename USER_OBJ_TYPE>
  class afifo_queue_result_iterator_tmpl
    {
  public:
    using user_obj_type = USER_OBJ_TYPE;
    using node_type = lifo_node_t<user_obj_type>;
    using pointer_type = node_type *;
    
  protected:
    node_type * linked_list_;
    
  public:
    afifo_queue_result_iterator_tmpl() noexcept : linked_list_{} {}
    explicit afifo_queue_result_iterator_tmpl( node_type * linked_list ) noexcept : linked_list_{linked_list} {}
    afifo_queue_result_iterator_tmpl( afifo_queue_result_iterator_tmpl && rh ) noexcept ;
    afifo_queue_result_iterator_tmpl & operator=( afifo_queue_result_iterator_tmpl && rh ) noexcept { swap( rh ); return *this; }
    
    bool empty() const noexcept { return linked_list_ == nullptr; }
    
    node_type * pull();
    void swap( afifo_queue_result_iterator_tmpl & rh ) noexcept;
    };
    
  template<typename T>
  afifo_queue_result_iterator_tmpl<T>::afifo_queue_result_iterator_tmpl( afifo_queue_result_iterator_tmpl && rh ) noexcept :
      linked_list_{rh.linked_list_} 
    {
    rh.linked_list_ = nullptr;
    }
  
  template<typename T>
  void afifo_queue_result_iterator_tmpl<T>::swap( afifo_queue_result_iterator_tmpl & rh ) noexcept
    {
    std::swap(linked_list_,rh.linked_list_);
    }
    
  template<typename T>
  typename afifo_queue_result_iterator_tmpl<T>::node_type *
  afifo_queue_result_iterator_tmpl<T>::pull()
    {
    node_type * result {};
    if( linked_list_ != nullptr )
      {
      result = linked_list_;
      linked_list_ = linked_list_->next;
      }
    return result;
    }
    
  //----------------------------------------------------------------------------------------------------------------------
  //
  // afifo_queue_internal_tmpl
  // aggregated pop queue
  //
  //----------------------------------------------------------------------------------------------------------------------

  ///\brief lifo aggregated pop queue used internaly for node managment
  template<typename USER_OBJ_TYPE>
  class afifo_queue_internal_tmpl
    {
  public:
    using user_obj_type = USER_OBJ_TYPE;
    using node_type = lifo_node_t<user_obj_type>;
    using pointer_type = node_type *;
    using size_type = long;
    
  private:
    pointer_type   head_;
    size_type      size_;
    bool           finish_wating_;
    
  public:
    inline bool        empty() const noexcept                  { return atomic_load( &head_, memorder::acquire ) == nullptr; }
    inline size_type   size() const noexcept                   { return atomic_load( &size_, memorder::acquire ); }
    inline bool        finish_waiting() const noexcept         { return finish_wating_; }
    inline void        finish_waiting( bool value ) noexcept   { finish_wating_ = value ; }
    
  public:
    afifo_queue_internal_tmpl() : head_{} , size_{}, finish_wating_{} {}
    ~afifo_queue_internal_tmpl(){}
    afifo_queue_internal_tmpl( afifo_queue_internal_tmpl const & ) = delete;
    afifo_queue_internal_tmpl & operator=( afifo_queue_internal_tmpl const & ) = delete;
    
  public:
    ///\brief enqueues supplyied node
    void push( node_type * user_data [[gnu::nonnull]] );
    
    ///\brief single try to dequeue entire linked list
    ///\description @{
    /// when queue is empty returns imediatly
    /// when queue is not empty it retries infinitie number of times until it succeeds or queue becomes empty
    ///@}
    ///\returns linked list of nodes with fifo order
    node_type * pull();
    
    ///\breif waits until pop succeeds with sleeping between retrys
    ///\param sleep_millisec time in miliseconds of sleeping
    node_type * pull_wait( size_t sleep_millisec );
    static node_type * reverse( node_type * node_llist ) noexcept;
    };
    
  template<typename T>
  void afifo_queue_internal_tmpl<T>::push( node_type * next_node [[gnu::nonnull]] )
    {
    if( !finish_waiting() )
      {
      //atomic linked list
      bool node_submitted {};
      do
        {
        pointer_type last_head { atomic_load( &head_, memorder::acquire ) };
        next_node->next = last_head;
        node_submitted = atomic_compare_exchange( &head_, last_head, next_node );
        }
      while(!node_submitted);
      atomic_add_fetch(&size_, size_type{1}, memorder::release );
      }
    }
    
  template<typename T>
  typename afifo_queue_internal_tmpl<T>::node_type * 
  afifo_queue_internal_tmpl<T>::pull()
    {
    pointer_type head_to_dequeue{atomic_load( &head_, memorder::acquire)};

    for (;nullptr != head_to_dequeue; //return when nothing left in queue
            head_to_dequeue = atomic_load( &head_, memorder::acquire))                                                 // Keep trying until Dequeue is done
      {
      bool deque_is_done = atomic_compare_exchange( &head_, head_to_dequeue, pointer_type{} );//if swap succeeds new head is estabilished
      if( deque_is_done )
        {
        size_type size_to_sub {1};
        for( auto node{head_to_dequeue->next}; node != nullptr; node = node->next)
           ++size_to_sub; 
        atomic_sub_fetch(&size_, size_to_sub, memorder::release );
        break;
        }
      }
    //reverse order for fifo, do any one needs here lifo order ?
    return reverse(head_to_dequeue);
    }
    
  template<typename T>
  typename afifo_queue_internal_tmpl<T>::node_type * 
  afifo_queue_internal_tmpl<T>::reverse( node_type * llist ) noexcept
    {
    node_type * prev {};
    for( ; nullptr != llist; )
      {
      node_type * next { llist->next };
      llist->next = prev;
      prev = llist;
      llist = next;
      }
    return prev;
    }
    
  //----------------------------------------------------------------------------------------------------------------------
  //
  // afifo_queue_t
  // aggregated pop queue
  //
  //----------------------------------------------------------------------------------------------------------------------
  template<typename USER_OBJ_TYPE>
  class afifo_queue_t ;
  
  template<typename USER_OBJ_TYPE>
  class afifo_queue_result_iterator_t :
      protected afifo_queue_result_iterator_tmpl<USER_OBJ_TYPE>
    {
  public:
    using user_obj_type = USER_OBJ_TYPE;
    using node_type = lifo_node_t<user_obj_type>;
    using pointer_type = node_type *;
    using parent_type = afifo_queue_t<user_obj_type>;
    using base_type = afifo_queue_result_iterator_tmpl<user_obj_type>;
    
  protected:
    parent_type * parent_;
    
  public:
    afifo_queue_result_iterator_t() noexcept : base_type{}, parent_{} {}
    afifo_queue_result_iterator_t(parent_type * parent,  std::unique_ptr<node_type> && llist) noexcept : 
      base_type{llist.get()}, parent_{parent} 
      { llist.release(); }
      
    afifo_queue_result_iterator_t( afifo_queue_result_iterator_t && rh ) noexcept ;
    afifo_queue_result_iterator_t & operator=( afifo_queue_result_iterator_t && rh ) noexcept { swap( rh ); return *this; }
    
    using base_type::empty;
    std::pair<user_obj_type, bool> pull();
    void swap( afifo_queue_result_iterator_t & rh ) noexcept;
    };
    
    
  template<typename T>
  afifo_queue_result_iterator_t<T>::afifo_queue_result_iterator_t( afifo_queue_result_iterator_t && rh ) noexcept :
      base_type{ std::move(rh)}, parent_{ rh.parent_}
    {
    rh.parent_ = nullptr;
    }
  
  template<typename T>
  void afifo_queue_result_iterator_t<T>::swap( afifo_queue_result_iterator_t & rh ) noexcept
    {
    base_type::swap(rh);
    std::swap(parent_,rh.parent_);
    }
    
  template<typename T>
  std::pair<typename afifo_queue_result_iterator_t<T>::user_obj_type, bool>
  afifo_queue_result_iterator_t<T>::pull()
    {
    std::unique_ptr<node_type> detached_node { base_type::pull() };
    
    if( nullptr != detached_node )
      {
      std::pair<user_obj_type, bool> result { std::move( detached_node->value ),  true };
      parent_->reuse_node( std::move(detached_node) );
      return result;
      }
    return {};
    }
    
  ///\brief lifo aggregated pop queue used internaly for node managment
  template<typename USER_OBJ_TYPE>
  class afifo_queue_t 
      : public afifo_queue_internal_tmpl<USER_OBJ_TYPE>
    {
  public:
    using user_obj_type =  USER_OBJ_TYPE;
    using base_type = afifo_queue_internal_tmpl<user_obj_type>;
    using reuse_node_queue_type = reuse_node_queue_t<user_obj_type>;
    using node_type = typename base_type::node_type;
    using pop_iterator_type = afifo_queue_result_iterator_t<user_obj_type>;
  private:
    reuse_node_queue_type free_node_to_reuse_;
    
  public:
    afifo_queue_t() : base_type(), free_node_to_reuse_() {}
    afifo_queue_t( afifo_queue_t const & ) = delete;
    afifo_queue_t & operator=( afifo_queue_t const & ) = delete;
    
    void push( user_obj_type && user_data );
    std::pair<pop_iterator_type, bool> pull();
    
    void reuse_node( std::unique_ptr<node_type> && to_reuse );
    };
    
  template<typename T>
  void afifo_queue_t<T>::push( user_obj_type && user_data )
    {
    std::unique_ptr<node_type> next_node { free_node_to_reuse_.construct_node(std::forward<user_obj_type &&>(user_data)) };
    base_type::push( next_node.get() );
    next_node.release();
    }
  
  template<typename T>
  std::pair<typename afifo_queue_t<T>::pop_iterator_type, bool>
  afifo_queue_t<T>::pull()
    {
    std::unique_ptr<node_type> node_list { base_type::pull() };
    bool success = static_cast<bool>(node_list);
    return {pop_iterator_type{ this, std::move(node_list) }, success };
    }
  
  template<typename T>
  void afifo_queue_t<T>::reuse_node( std::unique_ptr<node_type> && to_reuse )
    {
    free_node_to_reuse_.reuse_node( std::move(to_reuse) );
    }
  //----------------------------------------------------------------------------------------------------------------------
  //
  // fifo_queue_t
  //
  //----------------------------------------------------------------------------------------------------------------------
  template<typename USER_OBJ_TYPE>
  struct queue_envelope_t
    {
    using user_obj_type = USER_OBJ_TYPE ;
    user_obj_type value;
    
    queue_envelope_t( user_obj_type && v ) : value( std::forward<user_obj_type &&>(v)){}
    };
  
  template<typename USER_OBJ_TYPE>
  class fifo_queue_internal_tmpl
    {
  public:
    using user_obj_type =  USER_OBJ_TYPE;
    using node_type = node_t<user_obj_type *>;
    using pointer_type = pointer_t<node_type>;
    using user_obj_ptr_type = user_obj_type *;
    using reuse_node_queue_type = reuse_node_queue_t<user_obj_ptr_type>;
    using size_type = long;
    
  private:
    reuse_node_queue_type free_node_to_reuse_;
    pointer_type    m_head;
    pointer_type    m_tail;
    size_type       m_size;
    bool            m_finish_wating;
  public:
    bool        empty() const                   { return m_size == 0; }
    size_type   size() const                    { assert(m_size>=0); return m_size; }
    bool        finish_waiting() const          { return m_finish_wating; }
    void        finish_waiting( bool value )    { m_finish_wating = value ; }
  public:
    fifo_queue_internal_tmpl();
    ~fifo_queue_internal_tmpl();
    fifo_queue_internal_tmpl( fifo_queue_internal_tmpl const & ) = delete;
    fifo_queue_internal_tmpl & operator=( fifo_queue_internal_tmpl const & ) = delete;
  
  public:
    void push( user_obj_type * user_data );
    user_obj_type * pull();
    user_obj_type * pull_wait( size_t sleep_tm );
  };
  

  template<typename USER_OBJ_TYPE>
  fifo_queue_internal_tmpl<USER_OBJ_TYPE>::fifo_queue_internal_tmpl()
    : m_head()
    , m_tail()
    , m_size()
    , m_finish_wating()
  {
    node_type * node = new node_type(); // Allocate a free node
                      // Make it the only node in the linked list
    m_head.set_ptr( node );
    m_tail.set_ptr( node );        // Both Head and Tail point to it
    static_assert( sizeof(pointer_type) == 8, "64bit only supported" );
  }
  
  template<typename T>
  fifo_queue_internal_tmpl<T>::~fifo_queue_internal_tmpl()
    {
    try 
      {
      if( !m_finish_wating)
        m_finish_wating = true;

      user_obj_type * any_data;
      while ((any_data = pull()) != nullptr);
      delete m_head.get();
      }
    catch(...)
      {}
    }

  template<typename T>
  void fifo_queue_internal_tmpl<T>::push( user_obj_type * user_data )
    {
    pointer_type tail_local, next;
    // Allocate a new node from the free list
    
    std::unique_ptr<node_type> node{ free_node_to_reuse_.construct_node( user_obj_ptr_type{user_data} ) };
                                                                    // Set next pointer of node to NULL
    for(;;)                                                     // Keep trying until Enqueue is done
      {
      tail_local = atomic_load( m_tail, memorder::relaxed );        // Read Tail.ptr and Tail.count together
      node_type* node_1 = tail_local.get();
      next = atomic_load(node_1->next_cas, memorder::relaxed);      // Read next ptr and count fields together

      if ( tail_local == atomic_load( m_tail, memorder::relaxed ) ) // Are tail_local and next consistent?
        {
        if (next.get() == nullptr)                                  // Was Tail pointing to the last node?
          {
          if( cas( tail_local.get()->next_cas, next, { node.get(), next.count() + 1 } ) )  // Try to link node at the end of the linked list
            break;      // Enqueue is done.  Exit loop
          }
        else         // Tail was not pointing to the last node
          {
          cas( m_tail, tail_local, { next.get(), tail_local.count() + 1 } );// Try to swing Tail to the next node
          }
        }
      }
    cas( m_tail, tail_local, {node.get(), tail_local.count() + 1} );// Enqueue is done.  Try to swing Tail to the inserted node
    node.release();
    atomic_add_fetch( &m_size, size_type{1}, memorder::relaxed );
    }

  template<typename T>
  typename fifo_queue_internal_tmpl<T>::user_obj_type *
  fifo_queue_internal_tmpl<T>::pull()
    {
    user_obj_type * pvalue{};
    pointer_type head;

    for (;;)                                  // Keep trying until Dequeue is done
      {
      head = atomic_load(m_head, memorder::relaxed);    // Read Head
      pointer_type tail = atomic_load(m_tail, memorder::relaxed);    // Read Tail
      pointer_type next = atomic_load(head.get()->next_cas, memorder::relaxed);                           // Read Head.ptr->next //heap-use-after-free
      if( head == m_head )                             // Are head, tail, and next consistent?
        {
        if( head.get() == tail.get() )                     // Is queue empty or Tail falling behind?
          {
          if ( next.get() == nullptr)                       // Is queue empty?
            return nullptr;  
          cas( m_tail, tail, {next.get(), tail.count() + 1} );        // Tail is falling behind.  Try to advance it
          }
        else
          {
          // Read value before CAS
          //D12 Otherwise, another dequeue might free the next node
          node_type * node_d = next.get();
          if ( next.get() != nullptr) 
            {
            pvalue = node_d->value;
            // Try to swing Head to the next node
            if ( cas( m_head, head, {next.get(), head.count() + 1}))      // Try to swing Head to the next node
              break;
            pvalue = nullptr;
            }
          }
        }
      }
    std::unique_ptr<node_type> next_todel{  head.get() };
    next_todel->value = nullptr; //value is returned to user, dont leave here pointer
    
    // It is safe now to free the old node //heap-use-after-free
    free_node_to_reuse_.reuse_node( std::move(next_todel) );   
    head.set_ptr(nullptr);
    atomic_sub_fetch( &m_size, size_type{1}, memorder::relaxed );
    return pvalue;   // Queue was not empty, dequeue succeeded
    }

  template<typename T>
  typename fifo_queue_internal_tmpl<T>::user_obj_type *
  fifo_queue_internal_tmpl<T>::pull_wait( size_t sleep_tm )
    {
    for (;;)
      {
      user_obj_type * res = pull();

      if ( res != nullptr || m_finish_wating )
        return res;

      ampi::sleep( static_cast< uint32_t>( sleep_tm ));
      }
    }

  //----------------------------------------------------------------------------------------------------------------------
  //
  // fifo_queue_t
  //
  //----------------------------------------------------------------------------------------------------------------------
  
  template<typename USER_OBJ_TYPE>
  class fifo_queue_t
    : public fifo_queue_internal_tmpl<queue_envelope_t<USER_OBJ_TYPE>>
  {
  public:
    typedef USER_OBJ_TYPE user_obj_type;
    typedef queue_envelope_t<user_obj_type> envelope_type;
    typedef fifo_queue_internal_tmpl<envelope_type> base_type;

  public:
    fifo_queue_t() : base_type(){}
    ~fifo_queue_t()
      {
      try
        {
        if( !this->finish_waiting())
          this->finish_waiting( true );

        for(;;)
          {
          envelope_type * any_data = base_type::pull();
          if( any_data )
            delete any_data;
          else
            break;
          }
        
      } 
      catch(...)
        {}
      }
    void push( user_obj_type const & user_data ) {  base_type::push( new envelope_type( user_data ) ); }
    void push( user_obj_type && user_data ) { base_type::push( new envelope_type( std::move(user_data) ) ); }
    
    std::pair<user_obj_type,bool> pull()
      {
      std::unique_ptr<envelope_type> envelope{ base_type::pull() };
      if( envelope )  
        return { std::move(envelope->value), true };
      return {};
      }
      
     std::pair<user_obj_type,bool> pull_wait( size_t sleep_tm )
      {
      std::unique_ptr<envelope_type> envelope( base_type::pull_wait( sleep_tm ) );
      if( envelope )  
        return { std::move(envelope->value), true };
      return {};
      }
    };
    
  //----------------------------------------------------------------------------------------------------------------------
  //
  // common functional access methods
  //
  //----------------------------------------------------------------------------------------------------------------------
    
  template<typename queue_type, typename user_obj_type>
  void push( queue_type & queue, user_obj_type && user_data ) 
    {
    queue.push( std::forward<user_obj_type &&>(user_data) ) ;
    }
   
  template<typename queue_type>
  inline auto pull( queue_type & queue )
    {
    return queue.pull();
    }

}

