#include "sd_direct_publisher.h"

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    sd::direct::PublisherConfig config;
    auto publisher = sd::direct::CreateDirectPublisher(config);

    if (!publisher->Start())
    {
        std::cerr << "Failed to start direct publisher." << std::endl;
        return 1;
    }

    for (int i = 0; i < 10; ++i)
    {
        publisher->PublishBool("Robot/Enabled", (i % 2) == 0);
        publisher->PublishDouble("Drive/Speed", i * 0.5);
        publisher->PublishString("Status/Mode", (i % 2) == 0 ? "Auto" : "Teleop");
        publisher->FlushNow();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    publisher->Stop();

    std::cout << "Published seq=" << publisher->GetPublishedSeq() << std::endl;
    return 0;
}
