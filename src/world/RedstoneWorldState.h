#pragma once
#include "world/RedstoneDevice.h"
#include "world/Block.h"
#include "Config.h"
#include "world/PistonMotion.h"
#include <memory>
#include <glm/glm.hpp>

struct RedstoneWorkBudget {
    // Per invocation: due events, and sparse scan/graph/frontier entries.
    size_t events=Config::REDSTONE_EVENTS_PER_TICK;
    size_t propagation=Config::REDSTONE_PROPAGATION_PER_TICK;
};

struct RedstoneWorldState {
    enum class Phase { Ready, Lifecycle, Inputs, InputSample, Events, Outputs, OutputSample, Lamps, Consumers, Observations, Snapshots, Motions, Commit, Effects, Drive, Pistons };
    struct Node {
        uint64_t identity;
        Redstone::Device device;
        std::vector<Redstone::RedstoneScheduler::Paused> suspended;
        bool active=false,blocked=false;
    };
    std::map<Redstone::Position,Node> nodes;
    std::set<Redstone::Position> consumers;
    std::map<std::pair<int,int>,std::set<Redstone::Position>> components;
    std::set<std::pair<int,int>> activeChunks;
    std::map<Redstone::Position,uint8_t> containerSignals;
    std::set<Redstone::Position> frozenDust;
    std::map<Redstone::Position,BlockId> observations;
    Redstone::RedstoneScheduler scheduler;
    struct GraphBuild {
        enum class Stage {Nodes, Boundary, Prune, Propagate, Publish};
        Stage stage=Stage::Nodes;
        std::map<Redstone::Position,Redstone::DustPropagation::Wire> wires;
        std::map<Redstone::Position,std::vector<Redstone::Position>> adjacency;
        std::set<Redstone::Position> uncertain,pending;
        std::optional<Redstone::Position> cursor;
    };
    std::unique_ptr<GraphBuild> graph;
    std::unique_ptr<Redstone::DustPropagation> propagation;
    std::optional<Redstone::Position> cursor;
    std::map<Redstone::Position,BlockId> staged;
    bool evaluating=false;
    size_t motionCursor=0,driveCursor=0,effectCursor=0,motionRemaining=0,requestCursor=0;
    std::map<Redstone::Position,BlockId> committedBefore,externalEdits;
    std::vector<Redstone::PistonMotion> motionView;
    std::vector<Redstone::Position> readyConsumers;
    std::vector<std::pair<Redstone::Position,bool>> pistonRequests;
    std::vector<glm::ivec3> ignitions;
    std::vector<Redstone::PistonMotion> motions;
    std::map<Redstone::Position,uint64_t> reservations;
    std::vector<Redstone::PistonSweep> sweeps;
    uint64_t nextMotion=0;
    std::vector<std::set<std::pair<int,int>>> transactions;
    std::set<std::pair<int,int>> atomicMeshes;
    uint64_t replicaMotionTick=0;
    uint64_t nextIdentity=0;
    Phase phase=Phase::Ready;
    bool dirty=true,publishing=false,committingConsumers=false;
};
