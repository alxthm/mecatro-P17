#include <stdlib.h>
#include <stdio.h>
#include <string>

#include "behaviortree_cpp/bt_factory.h"
#include "behaviortree_cpp/loggers/bt_file_logger.h"
#include "behaviortree_cpp/loggers/bt_minitrace_logger.h"
#include "dynamixel_sdk.h"
#include <wiringPi.h>
#include "loguru.hpp"

#include "components/AX12.h"
#include "components/Kangaroo.h"
#include "components/LCD.h"
#include "components/SerialPort.h"
#include "components/UltrasonicSensor.h"

#include "strategy/Nodes.h"

using namespace std;
using namespace BT;


int main(int argc, char *argv[]) {
    // -----------------------
    // Initialize logger
    loguru::init(argc, argv);
    loguru::add_file("../log/everything.log", loguru::Append, loguru::Verbosity_MAX);
    loguru::add_file("../log/info.log", loguru::Append, loguru::Verbosity_INFO);


    // -----------------------
    // Initialize robot components
    // Kangaroo
    Kangaroo kangaroo(SERIAL_PORT_KANGAROO);
    if (kangaroo.isOperational())
        LOG_F(INFO, "Kangaroo is operational");
    else
        LOG_F(ERROR, "Kangaroo is not operational !");

    // AX-12
    dynamixel::PortHandler *portHandler = dynamixel::PortHandler::getPortHandler(DEVICENAME);
    dynamixel::PacketHandler *packetHandler = dynamixel::PacketHandler::getPacketHandler(PROTOCOL_VERSION);

    AX12 axPushRightAtom(AX_ID_BR_PUSH_RIGHT_ATOM, portHandler, packetHandler);
    AX12 axPushLeftAtom(AX_ID_BR_PUSH_LEFT_ATOM, portHandler, packetHandler);
    AX12 axMoveArmSide(AX_ID_BR_MOVE_ARM_SIDE, portHandler, packetHandler);
    AX12 axMoveArmFront(AX_ID_BR_MOVE_ARM_FRONT, portHandler, packetHandler);
    AX12 axTurnArm(AX_ID_BR_TURN_ARM, portHandler, packetHandler);

    // Other
    UltrasonicSensor frontSensor(SENSOR_TRIGGER_PIN, SENSOR_ECHO_PIN_FRONT);
    UltrasonicSensor backSensor(SENSOR_TRIGGER_PIN, SENSOR_ECHO_PIN_BACK);
    RelayModule pumpRelayModule(PUMP_RELAY_MODULE_PIN);
    RelayModule barrelRelayModule(BARREL_RELAY_MODULE_PIN);

    // -----------------------
    // Create the behavior tree

    // We use the BehaviorTreeFactory to register our custom nodes
    BehaviorTreeFactory factory;
    // Note: the name used to register should be the same used in the XML.
    using namespace Robot;

    // A node builder is nothing more than a function pointer to create a
    // std::unique_ptr<TreeNode>.
    // Using lambdas or std::bind, we can easily "inject" additional arguments.

    // Kangaroo
    NodeBuilder builderMoveAhead;
    builderMoveAhead = [&](const std::string &name, const NodeConfiguration &config) {
        return std::make_unique<MoveAhead>(name, config, frontSensor, backSensor, kangaroo);
    };
    NodeBuilder builderTurn;
    builderTurn = [&](const std::string &name, const NodeConfiguration &config) {
        return std::make_unique<Turn>(name, config, kangaroo);
    };
    factory.registerBuilder<MoveAhead>("MoveAhead", builderMoveAhead);
    factory.registerBuilder<Turn>("Turn", builderTurn);

    // AX-12
    NodeBuilder builderPushRightAtom = [&](const std::string &name, const NodeConfiguration &config) {
        return std::make_unique<MoveAX12Joint>(name, config, axPushRightAtom);
    };
    NodeBuilder builderPushLeftAtom = [&](const std::string &name, const NodeConfiguration &config) {
        return std::make_unique<MoveAX12Joint>(name, config, axPushLeftAtom);
    };
    NodeBuilder builderMoveArmFront = [&](const std::string &name, const NodeConfiguration &config) {
        return std::make_unique<MoveAX12Joint>(name, config, axMoveArmFront);
    };
    NodeBuilder builderTurnArm = [&](const std::string &name, const NodeConfiguration &config) {
        return std::make_unique<MoveAX12Joint>(name, config, axTurnArm);
    };
    NodeBuilder builderMoveArmSideJoint = [&](const std::string &name, const NodeConfiguration &config) {
        return std::make_unique<MoveAX12Joint>(name, config, axMoveArmSide);
    };
    NodeBuilder builderMoveArmSideWheel = [&](const std::string &name, const NodeConfiguration &config) {
        return std::make_unique<MoveAX12Wheel>(name, config, axMoveArmSide);
    };
    factory.registerBuilder<MoveAX12Joint>("PushRightAtom", builderPushRightAtom);
    factory.registerBuilder<MoveAX12Joint>("PushLeftAtom", builderPushLeftAtom);
    factory.registerBuilder<MoveAX12Joint>("MoveArmFront", builderMoveArmFront);
    factory.registerBuilder<MoveAX12Joint>("TurnArm", builderTurnArm);
    factory.registerBuilder<MoveAX12Wheel>("MoveArmSideWheel", builderMoveArmSideWheel);
    factory.registerBuilder<MoveAX12Joint>("MoveArmSideJoint", builderMoveArmSideJoint);

    // Other
    NodeBuilder builderActivatePump = [&](const std::string &name, const NodeConfiguration &config) {
        return std::make_unique<ActivateRelayModule>(name, config, pumpRelayModule);
    };
    NodeBuilder builderDeactivatePump = [&](const std::string &name, const NodeConfiguration &config) {
        return std::make_unique<DeactivateRelayModule>(name, config, pumpRelayModule);
    };
    NodeBuilder builderActivateBarrel = [&](const std::string &name, const NodeConfiguration &config) {
        return std::make_unique<ActivateRelayModule>(name, config, barrelRelayModule);
    };
    NodeBuilder builderDeactivateBarrel = [&](const std::string &name, const NodeConfiguration &config) {
        return std::make_unique<DeactivateRelayModule>(name, config, barrelRelayModule);
    };
    factory.registerBuilder<ActivateRelayModule>("ActivatePump", builderActivatePump);
    factory.registerBuilder<DeactivateRelayModule>("DeactivatePump", builderDeactivatePump);
    factory.registerBuilder<ActivateRelayModule>("ActivateBarrel", builderActivateBarrel);
    factory.registerBuilder<DeactivateRelayModule>("DeactivateBarrel", builderDeactivateBarrel);
    factory.registerNodeType<IsBarrelMoveFinished>("IsBarrelMoveFinished");

    // Trees are created at deployment-time (i.e. at run-time, but only
    // once at the beginning).

    // IMPORTANT: when the object "tree" goes out of scope, all the
    // TreeNodes are destroyed
    auto tree = factory.createTreeFromFile("/home/pi/mecatro_P17/src/strategy/tree_dev.xml"); // requires absolute paths

    // This logger saves state changes on file
    MinitraceLogger logger_minitrace(tree, "/home/pi/mecatro_P17/log/bt_trace.json");
    printTreeRecursively(tree.root_node);


    // -----------------------
    // Execute the behavior tree
    pumpRelayModule.turnOff();
    barrelRelayModule.turnOff();

    try {
        while (tree.root_node->executeTick() == NodeStatus::RUNNING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } catch (RuntimeError &e) {
        // stop th
        return -1;
    }

    return 0;
}