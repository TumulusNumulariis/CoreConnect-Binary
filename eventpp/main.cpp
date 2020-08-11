#include <iostream>
#include <include/eventpp/eventqueue.h>
#include <thread>
#include <event_engine2/h/ev_events.hpp>
#include <event_engine2/h/ev_manager.hpp>

using namespace std;

class mytestclass:  public ev_manager
{
    public:
    virtual int ev_app_event_callback(ev_event *pev){}
    virtual int ev_system_event_callback(int event){}
    //virtual int process_rx_frame(unsigned int index, const char *src, unsigned int len)

    enum states
        {
        task_initialise,
        };
    enum events
        {
        ev_startup = 0,
        ev_idle_tmr = 1,
        ev_load_tmr = 2,
        ev_tcp_accept = 7,
        ev_tcp_read = 8,
        ev_tcp_exception = 9,
        ev_console_read = 10,
        ev_write_latency_file = 12,
        ev_console_input = 13,
        ev_test_event_input1 = 14,
        ev_test_event_input2 = 15,
        };
    enum timers
        {
        tmr_idle,
        tmr_load,
        };
        int ev_run()
        {
            constexpr int stopEvent = 1;
            constexpr int otherEvent = 2;




            using EQ = eventpp::EventQueue<int, void (ev_event*)>;
            EQ queue;


            std::thread thread([stopEvent, otherEvent, &queue]() {
                volatile bool shouldStop = false;
                queue.appendListener(stopEvent, [&shouldStop](ev_event*) {
                    shouldStop = true;
                });
                queue.appendListener(otherEvent, [](const ev_event* pev) {
                    std::cout << "Got event, index is " << pev->m_event << std::endl;
                });

                while(! shouldStop) {
                    queue.wait();

                    queue.process();
                }
            });
            thread.join();
        }



};






constexpr int stopEvent = 1;
constexpr int otherEvent = 2;




using EQ = eventpp::EventQueue<int, void (ev_event*)>;
EQ queue;



std::thread thread([stopEvent, otherEvent, &queue]() {
	volatile bool shouldStop = false;
	queue.appendListener(stopEvent, [&shouldStop](ev_event*) {
		shouldStop = true;
	});
	queue.appendListener(otherEvent, [](const ev_event* pev) {
		std::cout << "Got event, index is " << pev->m_event << std::endl;
	});

	while(! shouldStop) {
		queue.wait();

		queue.process();
	}
});
int main()
{
    ev_event * e = new ev_event(5);
    queue.enqueue(otherEvent, e);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::cout << "Should have triggered event with index = 1" << std::endl;
    e->m_event =6;
    queue.enqueue(otherEvent, e);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::cout << "Should have triggered event with index = 2" << std::endl;



    return 0;
}