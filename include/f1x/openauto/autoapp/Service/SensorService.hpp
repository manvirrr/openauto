/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <f1x/aasdk/Channel/Sensor/SensorServiceChannel.hpp>
#include <f1x/openauto/autoapp/Service/IService.hpp>
#include <fstream>
#include <f1x/aasdk/IO/Promise.hpp>
#include <f1x/openauto/autoapp/Service/APDS9960_RPi.h>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace service
{

class SensorService: public aasdk::channel::sensor::ISensorServiceChannelEventHandler, public IService, public std::enable_shared_from_this<SensorService>
{
public:
    //aasdk::io::Promise<void> Promise;
    SensorService(boost::asio::io_service& ioService, aasdk::messenger::IMessenger::Pointer messenger);
    bool isNight = false;
    bool previous = false;
    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;
    void fillFeatures(aasdk::proto::messages::ServiceDiscoveryResponse& response) override;
    void onChannelOpenRequest(const aasdk::proto::messages::ChannelOpenRequest& request) override;
    void onSensorStartRequest(const aasdk::proto::messages::SensorStartRequestMessage& request) override;
    void onChannelError(const aasdk::error::Error& e) override;
    bool stopPolling = false;
    APDS9960_RPi apds = APDS9960_RPi();
    uint16_t ambient_light = 0;
    uint16_t red_light = 0;
    uint16_t green_light = 0;
    uint16_t blue_light = 0;
    //virtual void nightMessage(Promise::Pointer promise) = 0;
private:
    using std::enable_shared_from_this<SensorService>::shared_from_this;
    void sendDrivingStatusUnrestricted();
    void sendNightData();
    bool is_file_exist(const char *filename);
    void nightSensorPolling();
    bool readSensor();
    boost::asio::deadline_timer timer_;
    bool firstRun = true;
    //void onTimerExceeded(const boost::system::error_code& error)

    boost::asio::io_service::strand strand_;
    aasdk::channel::sensor::SensorServiceChannel::Pointer channel_;
    

    
};

}
}
}
}
