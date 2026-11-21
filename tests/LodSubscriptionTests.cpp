#include "network/LodSubscriptions.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
constexpr LodTileKey tile{-8,0,0};
auto priority=[](const LodTileKey& key){return double(std::abs(key.x));};

void replacementRace() {
    Lan::LodSubscriptions subscriptions;
    subscriptions.request(tile);
    const auto old=subscriptions.claimNext(priority);
    require(bool(old),"old subscription must produce work");
    subscriptions.cancel(tile);subscriptions.request(tile);
    const auto replacement=subscriptions.claimNext(priority);
    require(bool(replacement),"replacement subscription must produce independent work");
    bool oldConsumed=false,newDelivered=false;
    subscriptions.finish(*old,[&](bool){oldConsumed=true;return true;});
    require(!oldConsumed,"superseded subscription must not consume or deliver its old result");
    require(!subscriptions.claimNext(priority),"old completion must not release the replacement's pending work");
    subscriptions.finish(*replacement,[&](bool current){newDelivered=current;return current;});
    require(newDelivered&&!subscriptions.claimNext(priority),"replacement must deliver exactly once and become clean");
    subscriptions.cancel(tile);subscriptions.finish(*replacement,[](bool){throw std::runtime_error("cancelled delivery");return true;});
    require(!subscriptions.claimNext(priority),"cancelled work cannot resurrect a subscription");
}

void revisionsAndBackpressure() {
    Lan::LodSubscriptions subscriptions;
    subscriptions.request(tile);const auto initial=subscriptions.claimNext(priority);
    subscriptions.request(tile);
    require(!subscriptions.claimNext(priority),"duplicate request cannot schedule concurrent work for the same subscription");
    subscriptions.invalidateChunk(tile.x,tile.z);
    bool staleConsumed=false;
    subscriptions.finish(*initial,[&](bool current){require(!current,"edited data invalidates pending revision");staleConsumed=true;return false;});
    auto latest=subscriptions.claimNext(priority);
    require(staleConsumed&&bool(latest),"stale revision must be consumed and rescheduled");
    subscriptions.finish(*latest,[](bool current){require(current,"fresh data must be current");return false;});
    latest=subscriptions.claimNext(priority);
    require(bool(latest),"transport backpressure leaves work retryable");
    subscriptions.finish(*latest,[](bool current){return current;});
    require(!subscriptions.claimNext(priority),"successful delivery must not be repeated");
    subscriptions.request(tile);const auto beforeClear=subscriptions.claimNext(priority);
    subscriptions.clear();subscriptions.request(tile);const auto afterClear=subscriptions.claimNext(priority);
    subscriptions.finish(*beforeClear,[](bool){throw std::runtime_error("old lifetime delivery");return true;});
    require(!subscriptions.claimNext(priority),"clearing a stream must not reuse old subscription identities");
    subscriptions.finish(*afterClear,[](bool current){return current;});
}

void noticesRangeAndLimits() {
    Lan::LodSubscriptions subscriptions(2);
    const LodTileKey near{-3,0,0},outside{-30,0,0};
    subscriptions.request(tile);subscriptions.request(near);
    bool rejected=false;try{subscriptions.request(outside);}catch(const Lan::ProtocolError&){rejected=true;}
    require(rejected,"new subscriptions must respect the configured bound");
    auto selected=subscriptions.claimNext(priority);
    require(selected&&selected->key==near,"available work must follow caller priority");
    subscriptions.finish(*selected,[](bool current){return current;});
    subscriptions.invalidateChunk(tile.x,tile.z);
    bool saw=false;
    subscriptions.notifyInvalidations([](const auto&){return true;},[&](const auto& key,uint64_t){saw=key==tile;return false;});
    require(saw,"invalidation must identify the edited subscribed tile");
    saw=false;subscriptions.notifyInvalidations([](const auto&){return true;},[&](const auto&,uint64_t){saw=true;return true;});
    require(saw,"rejected invalidation notice must retry");
    subscriptions.notifyInvalidations([](const auto&){return true;},[](const auto&,uint64_t){throw std::runtime_error("duplicate notice");return true;});
    subscriptions.notifyInvalidations([&](const auto& key){return !(key==tile);},[](const auto&,uint64_t){return true;});
    subscriptions.request(outside); // Pruning released the bounded slot.
    auto remaining=subscriptions.claimNext(priority);
    require(remaining&&remaining->key==outside,"out-of-range subscriptions must not retain dirty work");
}

void replicaRevisionsAndBackpressure() {
    Lan::ReplicaLodSubscriptions replica;
    const std::array selected{tile};
    int attempts = 0;
    auto request = [&](const LodTileKey& key, uint64_t revision) {
        require(key == tile && revision == 0, "expected a tile request");
        return ++attempts > 1;
    };
    replica.poll(selected, request);
    replica.poll(selected, request);
    replica.poll(selected, request);
    require(attempts == 2, "failed sends retry and accepted requests stay pending");

    replica.invalidate(tile, 3);
    replica.invalidate(tile, 2);
    int applied = 0;
    auto apply = [&] { ++applied; };
    replica.receive(tile, 2, apply);
    require(applied == 0, "older invalidations and responses cannot lower the required revision");
    replica.poll(selected, request);
    require(attempts == 3, "invalidated tiles can be requested again");
    replica.receive(tile, 3, apply);
    replica.receive(tile, 3, apply);
    replica.poll(selected, request);
    require(applied == 1 && attempts == 3, "current data is applied once and completes the request");

    int cancellations = 0;
    auto cancel = [&](const LodTileKey& key, uint64_t revision) {
        require(key == tile && revision == Lan::CANCEL_LOD_SUBSCRIPTION, "expected a cancellation");
        return ++cancellations > 1;
    };
    replica.poll({}, cancel);
    replica.receive(tile, 4, apply);
    require(applied == 2, "failed cancellation keeps the live subscription");
    replica.poll({}, cancel);
    replica.receive(tile, 5, apply);
    require(applied == 2 && cancellations == 2, "late data cannot revive a cancelled subscription");

    replica.poll(selected, request);
    replica.clear();
    replica.receive(tile, 6, apply);
    replica.poll(selected, request);
    replica.receive(tile, 1, apply);
    require(applied == 3 && attempts == 5, "a new stream can request the same key from revision one");
}

void replicaRequestBudget() {
    Lan::ReplicaLodSubscriptions replica;
    std::array<LodTileKey, 10> selected{};
    for (int i = 0; i < 10; ++i) selected[i] = {-10 - i, 0, 0};

    size_t sent = 0;
    auto request = [&](const LodTileKey& key, uint64_t revision) {
        require(revision == 0 && key == selected.at(sent), "requests must follow selection order");
        ++sent;
        return true;
    };
    for (size_t i = 1; i <= 4; ++i) {
        replica.poll(selected, request);
        require(sent == i * 2, "each poll must respect the two-message budget");
    }
    replica.poll(selected, request);
    require(sent == 8, "no additional request when eight are pending");
    replica.receive(selected.front(), 1, [] {});
    replica.poll(selected, request);
    require(sent == 9, "receiving a column frees exactly one pending slot");
}
}

int main() try {
    replacementRace();revisionsAndBackpressure();noticesRangeAndLimits();
    replicaRevisionsAndBackpressure();replicaRequestBudget();
    std::cout<<"LOD subscription identity, revisions, backpressure and range tests passed\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}
