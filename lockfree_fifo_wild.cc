#include "lockfree_native.h"
#include <algorithm>
#include <numeric>
#include <future>
#include <chrono>

using std::chrono::microseconds;
using std::chrono::milliseconds;
#if defined(_LIBCPP_VERSION)
using clock_type = std::chrono::system_clock;
#else
using clock_type = std::chrono::high_resolution_clock;
#endif
using time_point = clock_type::time_point;

struct message_t
  { 
  uint32_t id;
  
  message_t() noexcept : id{} {}
  message_t(uint32_t pid ) noexcept : id{ pid } {}
  
  bool operator == ( message_t const & rh ) const noexcept { return id == rh.id; }
  bool operator != ( message_t const & rh ) const noexcept { return id != rh.id; }
  };
  
int main()
{
  using fifo_type = lockfree::fifo_queue_t<message_t>;
  
  fifo_type queue;
  uint32_t number_of_messages= 0x1FFFFF;
  uint32_t number_of_messages2= 0x1AFFFF;
  
  bool run {};
  auto recv = [&queue, &run]( uint32_t number_of_messages_loc )
                           {
                           while(! lockfree::atomic_load(run, lockfree::memorder::relaxed) )
                             lockfree::sleep(1);
                           
                           uint32_t recived_count{};
                           try {
                           do{
                              auto [result, succeed] = lockfree::queue_pop( queue );
                              if( succeed )
                                ++recived_count;
                            } while( recived_count != number_of_messages_loc );
                           }
                           catch(...){}
                           //SIGSEGV in release
//                            BOOST_TEST( recived_count == number_of_messages_loc );
                           return recived_count;
                           };
                           
  auto reciver = std::async(std::launch::async, recv, number_of_messages );
  auto reciver2 = std::async(std::launch::async, recv, number_of_messages2 );
  
  auto sender = std::async(std::launch::async,
                           [&queue, &run]( uint32_t number_of_messages_loc )
                            {
                            while(! lockfree::atomic_load(run, lockfree::memorder::relaxed) )
                              lockfree::sleep(1);
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
  auto tmbeg{ clock_type::now() };
  lockfree::atomic_add_fetch( &run, true, lockfree::memorder::relaxed );
//   printf("flags set\n");
  sender.get();
//   printf("sender finished\n");
  uint32_t sum = reciver.get() + reciver2.get();
  auto tmend{ clock_type::now() };
  auto dur{ std::chrono::duration_cast<milliseconds>(tmend-tmbeg) };
  printf("recivers finished %u == %u\n dur %lu\n",
         sum, number_of_messages+number_of_messages2,
         dur.count()
        );
//   BOOST_TEST( sum == (number_of_messages+number_of_messages2) );
return 0;  
}
