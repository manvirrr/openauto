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

#include <aasdk_proto/DrivingStatusEnum.pb.h>
#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/Service/SensorService.hpp>


namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace service
{

SensorService::SensorService(boost::asio::io_service& ioService, aasdk::messenger::IMessenger::Pointer messenger)
    : strand_(ioService)
    , timer_(ioService)
    , channel_(std::make_shared<aasdk::channel::sensor::SensorServiceChannel>(strand_, std::move(messenger)))
{

}

void SensorService::start()
{
    if ( apds.init() ) {
		OPENAUTO_LOG(info) << "APDS-9960 initialization complete" ;
	} else {
		OPENAUTO_LOG(info) << "Something went wrong during APDS-9960 init!" ;
	}

	// Start running the APDS-9960 light sensor (no interrupts)
	if ( apds.enableLightSensor(false) ) {
		OPENAUTO_LOG(info) << "Light sensor is now running" ;
	} else {
		OPENAUTO_LOG(info) << "Something went wrong during sensor init!";
	}

	// Wait for initialization and calibration to finish
	//delay(500);

    strand_.dispatch([this, self = this->shared_from_this()]() {
        //if(is_file_exist("/tmp/night_mode_enabled"))
        if(readSensor() || is_file_exist("/tmp/night_mode_enabled"))
        {
            this->isNight = true;
        }
        this->nightSensorPolling();
        OPENAUTO_LOG(info) << "[SensorService] start.";
        channel_->receive(this->shared_from_this());
    });
}

void SensorService::stop()
{
    this->stopPolling = true;
    strand_.dispatch([this, self = this->shared_from_this()]() {
        OPENAUTO_LOG(info) << "[SensorService] stop.";
    });
}

void SensorService::fillFeatures(aasdk::proto::messages::ServiceDiscoveryResponse& response)
{
    OPENAUTO_LOG(info) << "[SensorService] fill features.";

    auto* channelDescriptor = response.add_channels();
    channelDescriptor->set_channel_id(static_cast<uint32_t>(channel_->getId()));

    auto* sensorChannel = channelDescriptor->mutable_sensor_channel();
    sensorChannel->add_sensors()->set_type(aasdk::proto::enums::SensorType::DRIVING_STATUS);
    //sensorChannel->add_sensors()->set_type(aasdk::proto::enums::SensorType::LOCATION);
    sensorChannel->add_sensors()->set_type(aasdk::proto::enums::SensorType::NIGHT_DATA);
}

void SensorService::onChannelOpenRequest(const aasdk::proto::messages::ChannelOpenRequest& request)
{
    OPENAUTO_LOG(info) << "[SensorService] open request, priority: " << request.priority();
    const aasdk::proto::enums::Status::Enum status = aasdk::proto::enums::Status::OK;
    OPENAUTO_LOG(info) << "[SensorService] open status: " << status;

    aasdk::proto::messages::ChannelOpenResponse response;
    response.set_status(status);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendChannelOpenResponse(response, std::move(promise));

    channel_->receive(this->shared_from_this());
}

void SensorService::onSensorStartRequest(const aasdk::proto::messages::SensorStartRequestMessage& request)
{
    OPENAUTO_LOG(info) << "[SensorService] sensor start request, type: " << request.sensor_type();

    aasdk::proto::messages::SensorStartResponseMessage response;
    response.set_status(aasdk::proto::enums::Status::OK);

    auto promise = aasdk::channel::SendPromise::defer(strand_);

    if(request.sensor_type() == aasdk::proto::enums::SensorType::DRIVING_STATUS)
    {
        promise->then(std::bind(&SensorService::sendDrivingStatusUnrestricted, this->shared_from_this()),
                      std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    }
    else if(request.sensor_type() == aasdk::proto::enums::SensorType::NIGHT_DATA)
    {
        promise->then(std::bind(&SensorService::sendNightData, this->shared_from_this()),
                      std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    }
    else
    {
        promise->then([]() {}, std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    }

    channel_->sendSensorStartResponse(response, std::move(promise));
    channel_->receive(this->shared_from_this());
}

void SensorService::sendDrivingStatusUnrestricted()
{
    aasdk::proto::messages::SensorEventIndication indication;
    indication.add_driving_status()->set_status(aasdk::proto::enums::DrivingStatus::UNRESTRICTED);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
}

void SensorService::sendNightData()
{
    aasdk::proto::messages::SensorEventIndication indication;
    
    if(SensorService::isNight)
    {
	indication.add_night_mode()->set_is_night(true);
	OPENAUTO_LOG(info) << "Night Mode";
    }
    else
    {
	indication.add_night_mode()->set_is_night(false);
	OPENAUTO_LOG(info) << "Day Mode";
    }

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
    if(this->firstRun)
    {
        this->firstRun = false;
        this->previous = this->isNight;
    }
    OPENAUTO_LOG(info) << "Finished Mode Change";
}

void SensorService::onChannelError(const aasdk::error::Error& e)
{
    OPENAUTO_LOG(error) << "[SensorService] channel error: " << e.what();
}

bool SensorService::is_file_exist(const char *fileName)
{
    std::ifstream ifile(fileName, std::ios::in);
    return ifile.good();
}

void SensorService::nightSensorPolling()
{
    if(!this->stopPolling)
    {
        OPENAUTO_LOG(info) << "Polling";
        strand_.dispatch([this, self = this->shared_from_this()]() {
            //this->isNight = is_file_exist("/tmp/night_mode_enabled");
            if(readSensor() || is_file_exist("/tmp/night_mode_enabled"))
            {
                this->isNight = true;
            }
            else
            {
                this->isNight = false;
            }
            if(this->previous != this->isNight && !firstRun)
            {
                this->previous = this->isNight;
                this->sendNightData();
            }
            
            timer_.expires_from_now(boost::posix_time::seconds(2));
            //timer_.async_wait(this->nightSensorPolling());
            //timer_.expires_from_now(boost::posix_time::milliseconds(5000));
            //this->nightSensorPolling();
            timer_.async_wait(strand_.wrap(std::bind(&SensorService::nightSensorPolling, this->shared_from_this())));
        });
    }
}

bool SensorService::readSensor()
{
    if (!apds.readAmbientLight(ambient_light)) {
			 OPENAUTO_LOG(info) << "Error reading light values";
		}
	else {
        if(ambient_light < 10000)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    return false;
}
//void nightMessage(Promise::Pointer promise)
//{
  /*  strand_.dispatch([this, self = this->shared_from_this(), promise = std::move(promise)]() mutable {
        if(promise_ != nullptr)
        {
            promise_->reject(aasdk::error::Error(aasdk::error::ErrorCode::OPERATION_IN_PROGRESS));
        }
        else
        {
            if(this->previous != this-isNight)
            {
                SensorService.sendNightData();
            }
            promise_ = std::move(promise);
            timer_.expires_from_now(boost::posix_time::milliseconds(5000));
            timer_.async_wait(strand_.wrap(std::bind(&SensorService::onTimerExceeded, this->shared_from_this(), std::placeholders::_1)));
        }
    });*/
//}

/*void SensorService::onTimerExceeded(const boost::system::error_code& error)
{
    if(promise_ == nullptr)
    {
        return;
    }
    else if(error == boost::asio::error::operation_aborted || cancelled_)
    {
        promise_->reject(aasdk::error::Error(aasdk::error::ErrorCode::OPERATION_ABORTED));
    }
    else if(pingsCount_ - pongsCount_ > 1)
    {
        promise_->reject(aasdk::error::Error());
    }
    else
    {
        promise_->resolve();
    }

    promise_.reset();
}*/
/*bool is_file_exist(const std::string& name) {
  struct stat buffer;   
  return (stat (name.c_str(), &buffer) == 0); 
}*/

}
}
}
}
