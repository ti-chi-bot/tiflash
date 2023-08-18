// Copyright 2023 PingCAP, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//
// LevelFilterChannel.cpp
//
// $Id$
//
// Library: Ext
// Package: Logging
// Module:  LevelFilterChannel
//
// Copyright (c) 2004-2007, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
//
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include "Poco/Ext/LevelFilterChannel.h"

#include "Poco/LoggingRegistry.h"
#include "Poco/String.h"


namespace Poco
{


LevelFilterChannel::~LevelFilterChannel()
{
    if (_channel)
        _channel->release();
}


void LevelFilterChannel::setChannel(Channel * channel)
{
    if (_channel)
        _channel->release();
    _channel = channel;
    if (_channel)
        _channel->duplicate();
}


Channel * LevelFilterChannel::getChannel() const
{
    return _channel;
}

void LevelFilterChannel::open()
{
    if (_channel)
        _channel->open();
}


void LevelFilterChannel::close()
{
    if (_channel)
        _channel->close();
}


void LevelFilterChannel::setLevel(Message::Priority priority)
{
    _priority = priority;
}


void LevelFilterChannel::setLevel(const std::string & value)
{
    if (icompare(value, "fatal") == 0)
        setLevel(Message::PRIO_FATAL);
    else if (icompare(value, "critical") == 0)
        setLevel(Message::PRIO_CRITICAL);
    else if (icompare(value, "error") == 0)
        setLevel(Message::PRIO_ERROR);
    else if (icompare(value, "warning") == 0)
        setLevel(Message::PRIO_WARNING);
    else if (icompare(value, "notice") == 0)
        setLevel(Message::PRIO_NOTICE);
    else if (icompare(value, "information") == 0)
        setLevel(Message::PRIO_INFORMATION);
    else if (icompare(value, "debug") == 0)
        setLevel(Message::PRIO_DEBUG);
    else if (icompare(value, "trace") == 0)
        setLevel(Message::PRIO_TRACE);
    else
        throw InvalidArgumentException("Not a valid log value", value);
}


Message::Priority LevelFilterChannel::getLevel() const
{
    return _priority;
}


void LevelFilterChannel::setProperty(const std::string & name, const std::string & value)
{
    if (icompare(name, "level") == 0)
        setLevel(value);
    else if (icompare(name, "channel") == 0)
        setChannel(LoggingRegistry::defaultRegistry().channelForName(value));
    else
        Channel::setProperty(name, value);
}


void LevelFilterChannel::log(const Message & msg)
{
    if ((_priority >= msg.getPriority()) && _channel)
        _channel->log(msg);
}


} // namespace Poco
