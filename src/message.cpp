#include "message.hpp"
#include "server.hpp"
#include "Cereal.hpp"
#include <vector>
#include <iostream>


using namespace std;


MqttFixedHeader::MqttFixedHeader():
    type(), dup(), qos(), retain(), remaining() {
}

MqttFixedHeader::MqttFixedHeader(MqttType t, bool d, ubyte q, bool rt, uint re):
    type(t), dup(d), qos(q), retain(rt), remaining(re) {
}

void MqttFixedHeader::cerealise(Cereal& cereal) {
    cereal.grainBits(type, 4);
    cereal.grainBits(dup, 1);
    cereal.grainBits(qos, 2);
    cereal.grainBits(retain, 1);

    switch(cereal.getType()) {
    case Cereal::Type::Write:
        setRemainingSize(cereal);
        break;

    case Cereal::Type::Read:
        remaining = getRemainingSize(cereal);
        break;
    }
}

uint MqttFixedHeader::getRemainingSize(Cereal& cereal) {
    //algorithm straight from the MQTT spec
    int multiplier = 1;
    uint value = 0;
    ubyte digit;
    do {
        cereal.grain(digit);
        value += (digit & 127) * multiplier;
        multiplier *= 128;
    } while((digit & 128) != 0);

    return value;
}

void MqttFixedHeader::setRemainingSize(Cereal& cereal) const {
    //algorithm straight from the MQTT spec
    vector<ubyte> digits;
    uint x = remaining;
    do {
        ubyte digit = x % 128;
        x /= 128;
        if(x > 0) {
            digit = digit | 0x80;
        }
        digits.push_back(digit);
    } while(x > 0);

    for(auto b: digits) cereal.grain(b);
}


MqttConnect::MqttConnect(MqttFixedHeader h):header(h) { }

void MqttConnect::cerealise(Cereal& cereal) {
    cereal.grain(header);
    cereal.grain(protoName);
    cereal.grain(protoVersion);

    cereal.grainBits(hasUserName, 1);
    cereal.grainBits(hasPassword, 1);
    cereal.grainBits(hasWillRetain, 1);
    cereal.grainBits(willQos, 2);
    cereal.grainBits(hasWill, 1);
    cereal.grainBits(hasClear, 1);
    cereal.grainBits(reserved, 1);

    cereal.grain(keepAlive);
    cereal.grain(clientId);

    if(hasWill) cereal.grain(willTopic);
    if(hasWill) cereal.grain(willMessage);
    if(hasUserName) cereal.grain(userName);
    if(hasPassword) cereal.grain(password);
}


MqttConnack::MqttConnack():
    header(MqttType::CONNACK, false, 0, false, 2),
    reserved(),
    code(Code::ACCEPTED) {
}

MqttConnack::MqttConnack(Code c):
    MqttConnack() {

    code = c;
}

void MqttConnack::cerealise(Cereal& cereal) {
    cereal.grain(header);
    cereal.grain(reserved);
    cereal.grain(reinterpret_cast<ubyte&>(code));
}


MqttPublish::MqttPublish(MqttFixedHeader h):
    header(h), topic(), payload(), msgId() {

}

MqttPublish::MqttPublish(string t, std::vector<ubyte> p, ushort mid):
    MqttPublish(false, 0, false, t, p, mid) {
}

MqttPublish::MqttPublish(bool dup, ubyte qos, bool retain, string t,
                         std::vector<ubyte> p, ushort mid):
    topic(), payload(), msgId() {
    const auto topicLen = t.length() + 2; //2 for length
    auto remaining = qos ? topicLen + 2 /*msgId*/ : topicLen;
    remaining += p.size();

    header = MqttFixedHeader(MqttType::PUBLISH, dup, qos, retain, remaining);
    topic = t;
    payload = std::move(p);
    msgId = mid;
}

void MqttPublish::cerealise(Cereal& cereal) {
    cereal.grain(header);
    cereal.grain(topic);

    auto payloadLen = header.remaining - (topic.length() + MqttFixedHeader::SIZE);
    if(header.qos) {
        if(header.remaining < 7 && cereal.getType() == Cereal::Type::Read) {
            cerr << "Error: PUBLISH message with QOS but no message ID" << endl;
        } else {
            cereal.grain(msgId);
            payloadLen -= 2;
        }
    }
    if(cereal.getType() == Cereal::Type::Read) payload.resize(payloadLen);
    for(auto& b: payload) cereal.grain(b);
}


MqttSubscribe::MqttSubscribe(MqttFixedHeader h):
    header(h), msgId(), topics() {

}

void MqttSubscribe::Topic::cerealise(Cereal& cereal) {
    cereal.grain(topic);
    cereal.grain(qos);
}


void MqttSubscribe::cerealise(Cereal& cereal) {
    cereal.grain(header);
    cereal.grain(msgId);
    cereal.grainRawArray(topics);
}

MqttSuback::MqttSuback(MqttFixedHeader h):header(h) {

}

MqttSuback::MqttSuback(ushort m, std::vector<ubyte> q):
    header(MqttType::SUBACK, false, 0, false, q.size() + 2),
    msgId(m),
    qos(std::move(q)) {
}

void MqttSuback::cerealise(Cereal& cereal) {
    cereal.grain(header);
    cereal.grain(msgId);
    cereal.grainRawArray(qos);
}


MqttUnsubscribe::MqttUnsubscribe(MqttFixedHeader h):header(h) {

}

void MqttUnsubscribe::cerealise(Cereal& cereal) {
    cereal.grain(header);
    cereal.grain(msgId);
    cereal.grainRawArray(topics);
}


MqttUnsuback::MqttUnsuback(ushort m):
    header(MqttType::UNSUBACK, false, 0, false, 2),
    msgId(m)
{
}

MqttUnsuback::MqttUnsuback(MqttFixedHeader h):header(h), msgId() {

}

void MqttUnsuback::cerealise(Cereal& cereal) {
    cereal.grain(header);
    cereal.grain(msgId);
}

MqttType getMessageType(gsl::span<const ubyte> bytes) {
    return static_cast<MqttType>((bytes[0] >> 4));
}


gsl::cstring_span<> getPublishTopic(gsl::span<const ubyte> bytes) {
    if(bytes.size() < 4) {
        cerr << "Error: Can't get topic from a message of only " << bytes.size() << " bytes:\n";
        cerr << "[";
        for(const int b: bytes) {
            cerr << b << ", ";
        }
        cerr << "]" << endl;
        return {};
    }
    //won't work if there's a msg id and the size is < 128 bytes
    const auto topicLen = (bytes[2] << 8) + bytes[3];
    const auto charPtr = reinterpret_cast<const char*>(&bytes[4]);
    return {charPtr, topicLen};
}
