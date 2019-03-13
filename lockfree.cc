#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE LockFree
#include <boost/test/unit_test.hpp>
#include <ampi/ampi.h>
#include <algorithm>
#include <numeric>
#include <future>

struct message_t
  { 
  static int64_t instance_counter;
  uint32_t id;
  
  message_t() noexcept : id{} 
    { ampi::atomic_add_fetch(&instance_counter, int64_t(1), ampi::memorder::relaxed ); }
    
  message_t(uint32_t pid ) noexcept : id{ pid }
    { ampi::atomic_add_fetch(&instance_counter, int64_t(1), ampi::memorder::relaxed ); }

  message_t( message_t const & rh ) noexcept : id{ rh.id } 
    { ampi::atomic_add_fetch(&instance_counter, int64_t(1), ampi::memorder::relaxed ); }

  message_t & operator =( message_t const & rh ) noexcept
    {
    id = rh.id;
    return *this;
    }

  ~message_t() 
    { ampi::atomic_sub_fetch(&instance_counter, int64_t(1), ampi::memorder::relaxed ); }
  
  bool operator == ( message_t const & rh ) const noexcept { return id == rh.id; }
  bool operator != ( message_t const & rh ) const noexcept { return id != rh.id; }
  };

int64_t message_t::instance_counter = 0;

using afifo_queue_type = ampi::afifo_queue_t<message_t>;

static std::ostream & operator <<( std::ostream & stream, message_t sc )
  {
  stream << "id " << sc.id;
  return stream;
  }
  
BOOST_AUTO_TEST_CASE( lock_free_afifo_test_single )
{
  message_t::instance_counter  = 0;
  {
  afifo_queue_type queue;
  auto [it, succeed] { ampi::pull( queue ) };
  
  BOOST_TEST( !succeed );
  BOOST_TEST( it.empty() );
  
  ampi::push( queue, message_t{0} );
  std::tie(it, succeed) = ampi::pull( queue );
  
  message_t result;
  std::tie(result,succeed) = ampi::pull(it);
  
  BOOST_TEST( succeed );
  BOOST_TEST( result == (message_t{0}) );
  
  uint32_t i =1;
  for( ; i!=5; ++i)
    ampi::push( queue, message_t{i} );

  BOOST_TEST( !queue.empty() );  
  
  std::tie(it, succeed) = ampi::pull( queue );
  BOOST_TEST( succeed );
  BOOST_TEST( !it.empty() );
  
  
  for( i =1;!it.empty(); ++i )
    {
    std::tie(result,succeed) = ampi::pull(it);
    BOOST_TEST( succeed );
    BOOST_TEST( result == (message_t{i}) );
    }
  }
BOOST_TEST( message_t::instance_counter == 0 );
}

BOOST_AUTO_TEST_CASE( lock_free_afifo_test_2threads )
{
message_t::instance_counter  = 0;
  {
  afifo_queue_type queue;
  uint64_t number_of_messages= 0x1FFFF;
  bool sender_finished {};
  
  auto reciver = std::async(std::launch::async,
                           [&queue,number_of_messages,&sender_finished]()
                           {
                           uint32_t last_message_nr{};
                           uint64_t sum {};
                           uint64_t const expected_sum { ((number_of_messages-1)*number_of_messages)/2};
                           do
                            {
                             if( !queue.empty() )
                                {
                                auto [ it, succeed ] = ampi::pull( queue );
                                if( succeed )
                                  {
                                  for(;!it.empty();)
                                    {
                                    auto [ result, succeed2 ] = ampi::pull( it );
                                    BOOST_TEST( succeed2 );
                                    BOOST_TEST( result.id  == last_message_nr );
                                    sum = sum + result.id;
                                    BOOST_TEST( expected_sum >= sum );
                                    ++last_message_nr;
                                    }
                                  }
                                }
                             else
                               ampi::sleep(1);
                            }
                            while( !sender_finished || !queue.empty() );
                            
                           BOOST_TEST( number_of_messages == last_message_nr );
                           
                           BOOST_TEST( expected_sum == sum );
                           });
  
//   ampi::sleep(1);
  auto sender = std::async(std::launch::async,
                           [&queue,number_of_messages,&sender_finished]()
                            {
                            for( uint32_t i{}; i != number_of_messages; )
                              {
                              if( queue.size() <1000)
                                {
                                ampi::push( queue, message_t { i } );  
                                ++i;
                                }
                              else
                                ampi::sleep(1);
                              }
                            sender_finished = true;
                            });
  sender.get();
  printf("sender finished\n");
  reciver.get();
  }
BOOST_TEST( message_t::instance_counter == 0 );
}

BOOST_AUTO_TEST_CASE( lock_free_afifo_test_multiple_threads )
{
message_t::instance_counter  = 0;
  {
  afifo_queue_type queue;
  uint64_t number_of_messages= 0xFFFF;
  size_t number_of_senders = 16;
  bool sender_finished {};
  bool run {};
  
  
  auto fn_dequeue = [&queue,number_of_messages,&sender_finished,&run,number_of_senders]()
                    {
                    while(! ampi::atomic_load( &run, ampi::memorder::relaxed) )
                      ampi::sleep(1);
                    
                    uint32_t last_message_nr{};
                    uint64_t sum {};
                    uint64_t const expected_sum { ((number_of_messages-1)*number_of_messages)/2 * number_of_senders };
                    do
                    {
                      if( !queue.empty() )
                        {
                        auto [ it, succeed ] = ampi::pull( queue );
                        if( succeed )
                          {
                          for(;!it.empty();)
                            {
                            auto [ result, succeed2 ] = ampi::pull( it );
                            BOOST_TEST( succeed2 );
                            sum = sum + result.id;
                            BOOST_TEST( expected_sum >= sum );
                            ++last_message_nr;
                            }
                          }
                        }
                      else
                        ampi::sleep(1);
                    }
                    while( !ampi::atomic_load( &sender_finished, ampi::memorder::relaxed) || !queue.empty() );
                    
                    BOOST_TEST( (number_of_messages * number_of_senders) == last_message_nr );
                    
                    BOOST_TEST( expected_sum == sum );
                    };
                    
  auto fn_enqueue = [&queue,number_of_messages,&run]()
                    {
                    while(! ampi::atomic_load( &run, ampi::memorder::relaxed) )
                      ampi::sleep(1);
                    
                    for( uint32_t i{}; i != number_of_messages; )
                      {
                      if( queue.size() <1000)
                        {
                        ampi::push( queue, message_t { i } );  
                        ++i;
                        }
                      else
                        ampi::sleep(1);
                      }
                    };
                    
  auto reciver = std::async(std::launch::async, fn_dequeue );
  
  std::vector<std::future<void>> senders( number_of_senders );
  for( auto & sender : senders )
    sender = std::async(std::launch::async, fn_enqueue );
  
  ampi::atomic_add_fetch( &run, true, ampi::memorder::seq_cst );
  printf("flags set\n");

  for( auto & sender : senders )
    sender.get();
  ampi::atomic_add_fetch(&sender_finished,true, ampi::memorder::seq_cst );
  printf("sender finished\n");
  reciver.get();
  }
BOOST_TEST( message_t::instance_counter == 0 );
}

using lifo_type = ampi::lifo_queue_t<message_t>;
BOOST_AUTO_TEST_CASE( lock_free_lifo_test_single )
{
message_t::instance_counter  = 0;
  {
  lifo_type queue;
  ampi::push( queue, message_t { 0 } );  
  
  message_t result;
  bool res;
  std::tie(result,res) = ampi::pull( queue );
  BOOST_TEST( res );
  BOOST_TEST( result == (message_t{0}) );
  
  uint64_t number_of_messages= 0x1FFFF;
  for( uint32_t i{}; i != number_of_messages; ++i )
      ampi::push( queue, message_t { i } ); 
  
  BOOST_TEST( queue.size() == number_of_messages );
  BOOST_TEST( !queue.empty() );
  
  uint64_t const expected_sum { ((number_of_messages-1)*number_of_messages)/2};
  uint32_t last_message_nr{};
  uint64_t sum {};
  while( !queue.empty() )
    {
    auto [ result, succeed ] = ampi::pull( queue );
    if( succeed )
        {
        BOOST_TEST( result.id < number_of_messages );
        sum = sum + result.id;
        BOOST_TEST( expected_sum >= sum );
        ++last_message_nr;
        }
    }
  BOOST_TEST( number_of_messages == last_message_nr );
  BOOST_TEST( expected_sum == sum );
  BOOST_TEST( queue.empty() );
  BOOST_TEST( queue.size() == 0 );
  }
BOOST_TEST( message_t::instance_counter == 0 );
}

BOOST_AUTO_TEST_CASE( lock_free_lifo_test_2threads )
{
message_t::instance_counter  = 0;
  {
  lifo_type queue;
  uint64_t number_of_messages= 0x1FFFF;
  bool sender_finished {};
  
  auto reciver = std::async(std::launch::async,
                           [&queue,number_of_messages,&sender_finished]()
                           {
                           uint32_t last_message_nr{};
                           uint64_t sum {};
                           uint64_t const expected_sum { ((number_of_messages-1)*number_of_messages)/2};
                           do
                            {
//                              BOOST_TEST_CHECKPOINT( "last_message_nr" << last_message_nr);
                             if( !queue.empty() )
                                {
                                auto [ result, succeed ] = ampi::pull( queue );
                                if( succeed )
                                  {
                                  BOOST_TEST( result.id < number_of_messages );
                                  sum = sum + result.id;
  //                                 assert(expected_sum >= sum );
                                  BOOST_TEST( expected_sum >= sum );
                                  ++last_message_nr;
                                  }
                                }
                             else
                               ampi::sleep(1);
                            }
                            while( !sender_finished || !queue.empty() );
                            
                           BOOST_TEST( number_of_messages == last_message_nr );
                           
                           BOOST_TEST( expected_sum == sum );
                           });
  
//   ampi::sleep(1);
  auto sender = std::async(std::launch::async,
                           [&queue,number_of_messages,&sender_finished]()
                            {
                            for( uint32_t i{}; i != number_of_messages; )
                              {
                              if( queue.size() <1000)
                                {
                                ampi::push( queue, message_t { i } );  
                                ++i;
                                }
                              else
                                ampi::sleep(1);
                              }
                            sender_finished = true;
                            });
  sender.get();
  printf("sender finished\n");
  reciver.get();
  BOOST_TEST( queue.empty() );
  BOOST_TEST( queue.size() == 0 );
  }
BOOST_TEST( message_t::instance_counter == 0 );
}
BOOST_AUTO_TEST_CASE( lock_free_lifo_test_multiple_threads )
{
message_t::instance_counter  = 0;
  {
  lifo_type queue;
  uint64_t number_of_messages= 0x1FFFF;
  bool sender_finished {};
  bool run {};
  constexpr size_t number_of_senders = 16;
  
  auto reciver = std::async(std::launch::async,
                           [&queue,number_of_messages,&sender_finished,&run]()
                           {
                            while(! ampi::atomic_load( &run, ampi::memorder::relaxed) )
                              ampi::sleep(1);
                            
                           uint32_t last_message_nr{};
                           uint64_t sum {};
                           uint64_t const expected_sum { ((number_of_messages-1)*number_of_messages)/2 * number_of_senders };
                           do
                            {
//                              BOOST_TEST_CHECKPOINT( "last_message_nr" << last_message_nr);
                             if( !queue.empty() )
                                {
                                auto [ result, succeed ] = ampi::pull( queue );
                                if( succeed )
                                  {
                                  BOOST_TEST( result.id < number_of_messages );
                                  sum = sum + result.id;
                                  BOOST_TEST( expected_sum >= sum );
                                  ++last_message_nr;
                                  }
                                }
                             else
                               ampi::sleep(1);
                            }
                            while( !ampi::atomic_load( &sender_finished, ampi::memorder::relaxed) || !queue.empty() );
                            
                           BOOST_TEST( (number_of_messages * number_of_senders) == last_message_nr );
                           BOOST_TEST( expected_sum == sum );
                           });
  
//   ampi::sleep(1);
  auto fn_enqueue = [&queue,number_of_messages, &run]()
                    {
                    while(! ampi::atomic_load( &run, ampi::memorder::relaxed) )
                      ampi::sleep(1);
                    
                    for( uint32_t i{}; i != number_of_messages; )
                      {
                      if( queue.size() <1000)
                        {
                        ampi::push( queue, message_t { i } );  
                        ++i;
                        }
                      else
                        ampi::sleep(1);
                      }
                    };
  
  std::vector<std::future<void>> senders( number_of_senders );
  for( auto & sender : senders )
    sender = std::async(std::launch::async, fn_enqueue );
  
  ampi::atomic_add_fetch( &run, true, ampi::memorder::seq_cst );
  printf("flags set\n");
  
  for( auto & sender : senders )
    sender.get();
  printf("Senders send all data\n");
  ampi::atomic_add_fetch(&sender_finished,true, ampi::memorder::seq_cst );
  reciver.get();
  BOOST_TEST( queue.empty() );
  BOOST_TEST( queue.size() == 0 );
  }
BOOST_TEST( message_t::instance_counter == 0 );
}

using fifo_type = ampi::fifo_queue_t<message_t>;
BOOST_AUTO_TEST_CASE( lock_free_fifo_test_single )
{
message_t::instance_counter  = 0;
  {
  fifo_type queue;
  ampi::push( queue, message_t { 0 } );  
  
  message_t result;
  bool succeed;
  std::tie(result,succeed) = ampi::pull( queue );
  BOOST_TEST( succeed );
  BOOST_TEST( result == (message_t{0}) );
  
  ampi::push( queue, message_t { 1 } );
  ampi::push( queue, message_t { 2 } );
  std::tie(result,succeed) = ampi::pull( queue );
  BOOST_TEST( succeed );
  BOOST_TEST( result == (message_t{1}) );
  ampi::push( queue, message_t { 3 } );
  ampi::push( queue, message_t { 4 } );
  std::tie(result,succeed) = ampi::pull( queue );
  BOOST_TEST( succeed );
  BOOST_TEST( result == (message_t{2}) );
  
  std::tie(result,succeed) = ampi::pull( queue );
  BOOST_TEST( succeed );
  BOOST_TEST( result == (message_t{3}) );
  
  std::tie(result,succeed) = ampi::pull( queue );
  BOOST_TEST( succeed );
  BOOST_TEST( result == (message_t{4}) );
  BOOST_TEST( queue.empty() );
  BOOST_TEST( queue.size() == 0 );
  }
BOOST_TEST( message_t::instance_counter == 0 );
}

BOOST_AUTO_TEST_CASE( lock_free_fifo_test_2threads )
{
message_t::instance_counter  = 0;
  {
  fifo_type queue;
  uint32_t number_of_messages= 0x1FFFFF;
  auto reciver = std::async(std::launch::async,
                           [&queue,number_of_messages]()
                           {
                           uint32_t last_message_id{};
                           do{
                              auto[ result, succeed ] = ampi::pull( queue );
                              if(succeed)
                                {
                                BOOST_TEST( result == (message_t{last_message_id}) );
                                ++last_message_id;
                                }
                            } while( last_message_id != number_of_messages-1);
                           });
  
  ampi::sleep(10);
  auto sender = std::async(std::launch::async,
                           [&queue,number_of_messages]()
                            {
                            for( uint32_t i{}; i != number_of_messages; ++i )
                              ampi::push( queue, message_t { i } );  
                            });
  reciver.get();
  sender.get();
  }
BOOST_TEST( message_t::instance_counter == 0 );
}

BOOST_AUTO_TEST_CASE( lock_free_fifo_test_3threads_2recv_1send, * boost::unit_test::timeout(60) )
{
message_t::instance_counter  = 0;
  {
  fifo_type queue;
  uint32_t number_of_messages= 0xFFFFF;
  uint32_t number_of_messages2= 0xAFFFF;
  
  bool run {};
  auto recv = [&queue, &run]( uint32_t number_of_messages_loc )
                           {
                           while(! ampi::atomic_load( &run, ampi::memorder::relaxed) )
                             ampi::sleep(1);
                           printf("run recv..\n");
                           uint32_t recived_count{};
                           try {
                           do{
                              auto [result, succeed] = ampi::pull( queue );
                              if( succeed )
                                ++recived_count;
                            } while( recived_count != number_of_messages_loc );
                           }
                           catch(...){}
                           BOOST_TEST( recived_count == number_of_messages_loc );
                           return recived_count;
                           };
                           
  auto reciver = std::async(std::launch::async, recv, number_of_messages );
  auto reciver2 = std::async(std::launch::async, recv, number_of_messages2 );
  
  auto sender = std::async(std::launch::async,
                           [&queue, &run]( uint32_t number_of_messages_loc )
                            {
                            while(! ampi::atomic_load( &run, ampi::memorder::relaxed) )
                              ampi::sleep(1);
                            printf("run send..\n");
                            try {
                            for( uint32_t i{}; i != number_of_messages_loc;  )
                              if( queue.size() <1000)
                                {
                                ampi::push( queue, message_t { i } );  
                                ++i;
                                }
                              else
                                ampi::sleep(1);
                            }
                           catch(...){}
                            }, number_of_messages + number_of_messages2 );

  ampi::atomic_add_fetch( &run, true, ampi::memorder::seq_cst );
  printf("flags set\n");
  sender.get();
  printf("sender finished\n");
  uint32_t sum = reciver.get() + reciver2.get();
  printf("recivers finished\n");
  BOOST_TEST( sum == (number_of_messages+number_of_messages2) );
  BOOST_TEST( queue.empty() );
  BOOST_TEST( queue.size() == 0 );
  }
BOOST_TEST( message_t::instance_counter == 0 );
}

