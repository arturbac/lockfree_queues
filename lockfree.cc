#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE LockFree
#include <boost/test/unit_test.hpp>
#include "lockfree_native.h"
#include <algorithm>
#include <numeric>
#include <future>

struct message_t
  { 
  uint32_t id;
  
  message_t() noexcept : id{} {}
  message_t(uint32_t pid ) noexcept : id{ pid } {}
  
  bool operator == ( message_t const & rh ) const noexcept { return id == rh.id; }
  bool operator != ( message_t const & rh ) const noexcept { return id != rh.id; }
  };

  

using lifo_type = lockfree::lifo_queue_t<message_t>;

static std::ostream & operator <<( std::ostream & stream, message_t sc )
  {
  stream << "id " << sc.id;
  return stream;
  }
  
BOOST_AUTO_TEST_CASE( lock_free_lifo_test_single )
{
  lifo_type queue;
  queue.push( message_t { 0 } );  
  
  message_t result;
  bool res;
  std::tie(result,res) = queue.pop();
  BOOST_TEST( res );
  BOOST_TEST( result == (message_t{0}) );
  
  uint64_t number_of_messages= 0x1FFFF;
  for( uint32_t i{}; i != number_of_messages; ++i )
      queue.push( message_t { i } ); 
  
  BOOST_TEST( queue.size() == number_of_messages );
  BOOST_TEST( !queue.empty() );
  
  uint64_t const expected_sum { ((number_of_messages-1)*number_of_messages)/2};
  uint32_t last_message_nr{};
  uint64_t sum {};
  while( !queue.empty() )
    {
    auto [ result, succeed ] = queue.pop();
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
  BOOST_TEST( queue.size() == 0 );
}

BOOST_AUTO_TEST_CASE( lock_free_lifo_test_2threads )
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
                                auto [ result, succeed ] = queue.pop();
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
                               lockfree::sleep(1);
                            }
                            while( !sender_finished || !queue.empty() );
                            
                           BOOST_TEST( number_of_messages == last_message_nr );
                           
                           BOOST_TEST( expected_sum == sum );
                           });
  
//   lockfree::sleep(1);
  auto sender = std::async(std::launch::async,
                           [&queue,number_of_messages,&sender_finished]()
                            {
                            for( uint32_t i{}; i != number_of_messages; )
                              {
                              if( queue.size() <1000)
                                {
                                queue.push( message_t { i } );  
                                ++i;
                                }
                              else
                                lockfree::sleep(1);
                              }
                            sender_finished = true;
                            });
  sender.get();
  printf("sender finished\n");
  reciver.get();
  

}


using fifo_type = lockfree::fifo_queue_t<message_t>;
BOOST_AUTO_TEST_CASE( lock_free_fifo_test_single )
{
  fifo_type queue;
  queue.push( message_t { 0 } );  
  
  message_t result;
  bool succeed;
  std::tie(result,succeed) = lockfree::queue_pop( queue );
  BOOST_TEST( succeed );
  BOOST_TEST( result == (message_t{0}) );
  
  queue.push( message_t { 1 } );
  queue.push( message_t { 2 } );
  std::tie(result,succeed) = lockfree::queue_pop( queue );
  BOOST_TEST( succeed );
  BOOST_TEST( result == (message_t{1}) );
  queue.push( message_t { 3 } );
  queue.push( message_t { 4 } );
  std::tie(result,succeed) = lockfree::queue_pop( queue );
  BOOST_TEST( succeed );
  BOOST_TEST( result == (message_t{2}) );
  
  std::tie(result,succeed) = lockfree::queue_pop( queue );
  BOOST_TEST( succeed );
  BOOST_TEST( result == (message_t{3}) );
  
  std::tie(result,succeed) = lockfree::queue_pop( queue );
  BOOST_TEST( succeed );
  BOOST_TEST( result == (message_t{4}) );
}

BOOST_AUTO_TEST_CASE( lock_free_fifo_test_2threads )
{
  fifo_type queue;
  uint32_t number_of_messages= 0x1FFFFF;
  auto reciver = std::async(std::launch::async,
                           [&queue,number_of_messages]()
                           {
                           uint32_t last_message_id{};
                           do{
                              auto[ result, succeed ] = lockfree::queue_pop( queue );
                              if(succeed)
                                {
                                BOOST_TEST( result == (message_t{last_message_id}) );
                                ++last_message_id;
                                }
                            } while( last_message_id != number_of_messages-1);
                           });
  
  lockfree::sleep(10);
  auto sender = std::async(std::launch::async,
                           [&queue,number_of_messages]()
                            {
                            for( uint32_t i{}; i != number_of_messages; ++i )
                              queue.push( message_t { i } );  
                            });
  reciver.get();
  sender.get();

}

BOOST_AUTO_TEST_CASE( lock_free_fifo_test_3threads_2recv_1send, * boost::unit_test::timeout(60) )
{
  fifo_type queue;
  uint32_t number_of_messages= 0x1FFFFF;
  uint32_t number_of_messages2= 0x1AFFFF;
  
  bool run {};
  auto recv = [&queue, &run]( uint32_t number_of_messages_loc )
                           {
                           while(! lockfree::atomic_load(run, lockfree::memorder::relaxed) )
                             lockfree::sleep(1);
                           printf("run recv..\n");
                           uint32_t recived_count{};
                           try {
                           do{
                              auto [result, succeed] = lockfree::queue_pop( queue );
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
                            while(! lockfree::atomic_load(run, lockfree::memorder::relaxed) )
                              lockfree::sleep(1);
                            printf("run send..\n");
                            try {
                            for( uint32_t i{}; i != number_of_messages_loc;  )
                              if( queue.size() <1000)
                                {
                                queue.push( message_t { i } );  
                                ++i;
                                }
                              else
                                lockfree::sleep(1);
                            }
                           catch(...){}
                            }, number_of_messages + number_of_messages2 );

  lockfree::atomic_add_fetch( &run, true, lockfree::memorder::relaxed );
  printf("flags set\n");
  sender.get();
  printf("sender finished\n");
  uint32_t sum = reciver.get() + reciver2.get();
  printf("recivers finished\n");
  BOOST_TEST( sum == (number_of_messages+number_of_messages2) );
  
}

