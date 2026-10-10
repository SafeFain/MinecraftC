#include "core/NetworkSocket.h"
#include "network/Connection.h"
#include "network/Protocol.h"
#include "network/Session.h"
#include "network/PlayerProfile.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>

namespace {
void check(bool passed,const char* message) { if(!passed) { std::cerr<<message<<'\n'; std::exit(1); } }
template<class F> void rejects(F function) { bool rejected=false; try { function(); } catch(const Lan::ProtocolError&) { rejected=true; } check(rejected,"Invalid message accepted"); }
}
int main() {
    using namespace Lan;
    Writer writer; writer.u8(255);writer.u16(0x1234);writer.u32(0x12345678);writer.u64(UINT64_MAX);writer.f32(-1.25f);writer.f64(-4096.5);writer.text("局域网");
    Reader reader(writer.bytes);
    check(reader.u8()==255 && reader.u16()==0x1234 && reader.u32()==0x12345678 && reader.u64()==UINT64_MAX,"Integer byte order failed");
    check(reader.f32()==-1.25f && reader.f64()==-4096.5 && reader.text()=="局域网","Scalar/string roundtrip failed");reader.finish();
    rejects([&] { reader.u8(); });
    rejects([] { Writer value;value.f64(std::numeric_limits<double>::infinity()); });
    rejects([] { Bytes bytes{0,0,128,127};Reader value(bytes);value.f32(); });
    rejects([] { Writer value;value.text(std::string("a\0b",3)); });
    rejects([] { Writer value;value.text(std::string("\xc0\x80",2)); });
    rejects([] { Bytes bytes{2,0,0xc0,0x80};Reader value(bytes);value.text(); });
    auto encoded=encode({MessageType::Chat,42,writer.bytes});
    Decoder decoder;Message message;
    for(auto byte:encoded) decoder.feed(&byte,1);
    check(decoder.pop(message) && message.sequence==42 && message.payload==writer.bytes,"Fragmented TCP frame failed");
    check(!decoder.pop(message),"Duplicate frame");
    Bytes combined=encoded;combined.insert(combined.end(),encoded.begin(),encoded.end());decoder.feed(combined.data(),combined.size());
    check(decoder.pop(message) && decoder.pop(message) && !decoder.pop(message),"Coalesced TCP frames failed");
    for(size_t offset:{size_t(0),size_t(4),size_t(6),size_t(10)}) {
        auto bad=encoded;bad[offset]=255;bad[offset+1]=255;
        rejects([&] { Decoder value;value.feed(bad.data(),20); });
    }
    rejects([] { Decoder value;Bytes bytes(MAX_QUEUED_BYTES+1);value.feed(bytes.data(),bytes.size()); });
    Platform::NetworkSocket listener;
    const bool listening=listener.listen(0,true);check(listening,listener.error().c_str());
    const auto port=listener.localPort();check(port!=0,"Listener has no port");
    auto socket=std::make_unique<Platform::NetworkSocket>();const bool connecting=socket->connect("::1",port);check(connecting,socket->error().c_str());
    Connection client(std::move(socket));std::unique_ptr<Connection> server;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!server && std::chrono::steady_clock::now()<deadline) {
        client.poll();auto accepted=listener.accept();if(accepted) server=std::make_unique<Connection>(std::move(accepted));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(server!=nullptr,"Loopback accept timed out");
    check(client.queue({MessageType::Chat,7,writer.bytes}),"Queue failed");bool received=false;
    while(!received && std::chrono::steady_clock::now()<deadline) {
        client.poll();server->poll();received=server->pop(message);std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(received && message.sequence==7 && message.payload==writer.bytes,"Real TCP framing failed");
    server->close();
    while(client.alive() && std::chrono::steady_clock::now()<deadline) { client.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    check(!client.alive(),"Remote disconnect was not detected");
    auto invalid=std::make_unique<Platform::NetworkSocket>();check(!invalid->connect("not-an-address",port),"Invalid endpoint accepted");
    Platform::NetworkSocket conflict;check(!conflict.listen(port,true),"Port conflict accepted");
    Connection bounded(std::make_unique<Platform::NetworkSocket>());Bytes payload(MAX_PAYLOAD);
    for(int i=0;i<3;++i) check(bounded.queue({MessageType::ChunkSnapshot,0,payload}),"Send queue rejected valid frame");
    check(!bounded.queue({MessageType::ChunkSnapshot,0,payload}) && !bounded.alive(),"Send queue is unbounded");
    Host host;Compatibility compatibility{"test",20,123,{}};
    check(host.open(0,compatibility,2,true),"Room open failed");
    Identity identity{randomToken(),randomToken(),"Player"};
    Client first;check(first.join("::1",host.port(),compatibility,identity,0),"Room join failed");
    double now=0;size_t joined=0;
    for(int i=0;i<1000 && first.state()!=Client::State::Connected;++i) {
        now+=.001;joined+=host.poll(now).joined.size();first.poll(now);std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(first.state()==Client::State::Connected && joined==1,"Two-channel handshake failed");
    check(first.send({MessageType::Chat,1,writer.bytes}),"Connected peer send failed");received=false;
    for(int i=0;i<1000 && !received;++i) {
        now+=.001;first.poll(now);for(const auto& event:host.poll(now).messages) received=event.message.type==MessageType::Chat && event.message.payload==writer.bytes;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(received,"Session message missing");
    Bytes large(MAX_PAYLOAD,37);
    check(host.send(first.peerId(),{MessageType::ChunkSnapshot,0,large},true),"Bulk send failed");
    check(host.send(first.peerId(),{MessageType::PlayerState,0,writer.bytes}),"Control send failed");
    bool controlReceived=false,bulkReceived=false;
    for(int i=0;i<1000 && !bulkReceived;++i) {
        now+=.001;host.poll(now);
        for(const auto& event:first.poll(now)) {
            if(event.message.type==MessageType::PlayerState) controlReceived=true;
            if(event.message.type==MessageType::ChunkSnapshot) {
                check(controlReceived,"Bulk stream blocked control channel");
                check(event.chunks && event.message.payload==large,"Bulk channel data corrupted");bulkReceived=true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(controlReceived && bulkReceived,"Two-channel transfer timed out");
    Client full;Identity secondIdentity{randomToken(),randomToken(),"Second"};
    check(full.join("::1",host.port(),compatibility,secondIdentity,now),"Second connection failed");
    for(int i=0;i<1000 && full.state()!=Client::State::Failed;++i) {
        now+=.001;host.poll(now);first.poll(now);full.poll(now);std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(full.state()==Client::State::Failed && full.error()=="LAN room is full","Full room was not rejected");
    first.close();
    for(int i=0;i<1000 && !host.peers().empty();++i) {now+=.001;host.poll(now);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(host.peers().empty(),"Disconnected peer retained");
    Client incompatible;auto badCompatibility=compatibility;badCompatibility.generationVersion=19;
    check(incompatible.join("::1",host.port(),badCompatibility,identity,now),"Incompatible connect failed prematurely");
    for(int i=0;i<1000 && incompatible.state()!=Client::State::Failed;++i) {
        now+=.001;host.poll(now);incompatible.poll(now);std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(incompatible.state()==Client::State::Failed && host.peers().empty(),"Incompatible peer admitted");
    // Full descriptions, rather than the fast signature alone, gate admission.
    auto modded=compatibility;modded.content={{"plugin/example","1.0.0:data-api1"},{"config/example","{}"},{"block/example:ore","solid"}};
    modded.contentSignature=contentSignature(modded.content);
    host.close();check(host.open(0,modded,2,true),"Modded host opens");
    int admitted=0;host.admit=[&](const Identity&){++admitted;return true;};
    for(int scenario=0;scenario<5;++scenario) {
        auto mismatch=modded;
        if(scenario==0)mismatch.content.erase("plugin/example");
        if(scenario==1)mismatch.content["plugin/extra"]="1.0.0";
        if(scenario==2)mismatch.content["plugin/example"]="2.0.0:data-api1";
        if(scenario==3)mismatch.content["config/example"]="{\"setting\":1}";
        if(scenario==4)mismatch.content["block/example:ore"]="nonsolid";
        Client rejected;check(rejected.join("::1",host.port(),mismatch,identity,now),"Mismatched connection starts");
        for(int i=0;i<1000&&rejected.state()!=Client::State::Failed;++i) {
            now+=.001;host.poll(now);rejected.poll(now);std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(rejected.state()==Client::State::Failed&&admitted==0&&host.peers().empty(),"Content mismatch rejected before admission despite matching signature");
        check(rejected.error().find("requirement")!=std::string::npos,"Content mismatch names its requirement");
    }
    Client matched;check(matched.join("::1",host.port(),modded,identity,now),"Matching plugin connection starts");
    for(int i=0;i<1000&&matched.state()!=Client::State::Connected;++i) {
        now+=.001;host.poll(now);matched.poll(now);std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(matched.state()==Client::State::Connected&&admitted==1,"Matching description admits one player");
    check(matched.send({MessageType::Input,0,writer.bytes}),"Pending client intent queues before terminal fault");
    check(matched.send({MessageType::Input,0,writer.bytes}),"Second pending intent queues before terminal fault");
    host.close("Plugin callback failed: example");
    for(int i=0;i<50&&matched.state()!=Client::State::Failed;++i){now+=.005;matched.poll(now);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(matched.state()==Client::State::Failed&&matched.error()=="Plugin callback failed: example","Terminal plugin failure reason delivered");
    host.admit={};check(host.open(0,compatibility,2,true),"Reopen baseline host");
    Client timeout;check(timeout.join("::1",host.port(),compatibility,identity,now),"Timeout connect failed");
    timeout.poll(now+11);check(timeout.state()==Client::State::Failed,"Handshake deadline ignored");
    host.close();
    const auto profileDirectory=std::filesystem::temp_directory_path()/("minecraftc-lan-test-"+randomToken());
    ProfileStore profiles(profileDirectory/"world"/"players");
    PlayerProfile profile;profile.identity=ProfileStore::localIdentity(profileDirectory/"client");
    profile.positions[0]={-2048.5,65.25,4096.5};profile.positioned[0]=true;profile.dimension=DimensionId::Heaven;
    profile.bedSpawn=glm::ivec3(-4,70,-8);profile.inventory.slot(0)={ItemId::DIAMOND,12,0};
    auto profileBytes=encodeProfile(profile);auto restored=decodeProfile(profileBytes);
    check(restored.positions[0]==profile.positions[0] && restored.inventory.count(ItemId::DIAMOND)==12 && restored.bedSpawn==profile.bedSpawn,"Player profile roundtrip failed");
    for(uint16_t version:{uint16_t(1),uint16_t(2)}) {
        Reader paletteReader(profileBytes);paletteReader.u32();paletteReader.u16();const auto count=paletteReader.u16();
        for(uint16_t i=0;i<count;++i){paletteReader.u16();paletteReader.text(256);}
        const size_t offset=profileBytes.size()-paletteReader.remaining();
        Bytes legacy(profileBytes.begin(),profileBytes.begin()+6);legacy[4]=static_cast<uint8_t>(version);legacy[5]=0;
        const size_t end=profileBytes.size()-8-(version==1?50:0);
        legacy.insert(legacy.end(),profileBytes.begin()+offset,profileBytes.begin()+end);
        uint64_t checksum=14695981039346656037ULL;for(auto byte:legacy){checksum^=byte;checksum*=1099511628211ULL;}
        Writer trailer;trailer.u64(checksum);legacy.insert(legacy.end(),trailer.bytes.begin(),trailer.bytes.end());
        check(decodeProfile(legacy).inventory.count(ItemId::DIAMOND)==12,"Legacy profile version remains readable");
    }
    profileBytes[20]^=1;rejects([&]{decodeProfile(profileBytes);});
    profiles.save(profile);auto reloaded=profiles.load(profile.identity);check(reloaded && reloaded->inventory.count(ItemId::DIAMOND)==12,"Persistent player data missing");
    check(ProfileStore::localIdentity(profileDirectory/"client").id==profile.identity.id,"Local identity changed on restart");
    auto forged=profile.identity;forged.credential=randomToken();check(!profiles.accepts(forged),"Forged profile credential accepted");
    rejects([&]{profiles.load(forged);});
    forged.id="../../escape";check(!profiles.accepts(forged),"Unsafe profile filename accepted");
    std::filesystem::remove_all(profileDirectory);
    std::cout<<"LAN protocol, TCP, sessions and profile tests passed\n";
}
