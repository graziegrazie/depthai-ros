#include "depthai_ros_driver/pipeline_generator.hpp"

#include "depthai/device/Device.hpp"
#include "depthai/pipeline/Pipeline.hpp"
#include "depthai_ros_driver/dai_nodes/nn/nn_helpers.hpp"
#include "depthai_ros_driver/dai_nodes/nn/nn_wrapper.hpp"
#include "depthai_ros_driver/dai_nodes/nn/spatial_nn_wrapper.hpp"
#include "depthai_ros_driver/dai_nodes/sensors/imu.hpp"
#include "depthai_ros_driver/dai_nodes/sensors/sensor_helpers.hpp"
#include "depthai_ros_driver/dai_nodes/sensors/sensor_wrapper.hpp"
#include "depthai_ros_driver/dai_nodes/stereo.hpp"
#include "depthai_ros_driver/utils.hpp"
#include "rclcpp/node.hpp"

namespace depthai_ros_driver {
namespace pipeline_gen {
std::vector<std::unique_ptr<dai_nodes::BaseNode>> PipelineGenerator::createPipeline(rclcpp::Node* node,
                                                                                    std::shared_ptr<dai::Device> device,
                                                                                    std::shared_ptr<dai::Pipeline> pipeline,
                                                                                    const std::string& pipelineType,
                                                                                    const std::string& nnType,
                                                                                    bool enableImu) {
    RCLCPP_INFO(node->get_logger(), "Pipeline type: %s", pipelineType.c_str());
    std::string pTypeUpCase = utils::getUpperCaseStr(pipelineType);
    std::string nTypeUpCase = utils::getUpperCaseStr(nnType);
    auto pType = utils::getValFromMap(pTypeUpCase, pipelineTypeMap);
    pType = validatePipeline(node, pType, device->getCameraSensorNames().size());
    auto nType = utils::getValFromMap(nTypeUpCase, nnTypeMap);
    std::vector<std::unique_ptr<dai_nodes::BaseNode>> daiNodes;
    switch(pType) {
        case PipelineType::RGB: {
            auto rgb = std::make_unique<dai_nodes::SensorWrapper>("rgb", node, pipeline, device, dai::CameraBoardSocket::RGB);
            switch(nType) {
                case NNType::None:
                    break;
                case NNType::RGB: {
                    auto nn = createNN(node, pipeline, *rgb);
                    daiNodes.push_back(std::move(nn));
                    break;
                }
                case NNType::Spatial: {
                    RCLCPP_WARN(node->get_logger(), "Spatial NN selected, but configuration is RGB.");
                }
                default:
                    break;
            }
            daiNodes.push_back(std::move(rgb));
            break;
        }

        case PipelineType::RGBD: {
            auto rgb = std::make_unique<dai_nodes::SensorWrapper>("rgb", node, pipeline, device, dai::CameraBoardSocket::RGB);
            auto stereo = std::make_unique<dai_nodes::Stereo>("stereo", node, pipeline, device);
            switch(nType) {
                case NNType::None:
                    break;
                case NNType::RGB: {
                    auto nn = createNN(node, pipeline, *rgb);
                    daiNodes.push_back(std::move(nn));
                    break;
                }
                case NNType::Spatial: {
                    auto nn = createSpatialNN(node, pipeline, *rgb, *stereo);
                    daiNodes.push_back(std::move(nn));
                    break;
                }
                default:
                    break;
            }
            daiNodes.push_back(std::move(rgb));
            daiNodes.push_back(std::move(stereo));
            break;
        }
        case PipelineType::RGBStereo: {
            auto rgb = std::make_unique<dai_nodes::SensorWrapper>("rgb", node, pipeline, device, dai::CameraBoardSocket::RGB);
            auto left = std::make_unique<dai_nodes::SensorWrapper>("left", node, pipeline, device, dai::CameraBoardSocket::LEFT);
            auto right = std::make_unique<dai_nodes::SensorWrapper>("right", node, pipeline, device, dai::CameraBoardSocket::RIGHT);
            switch(nType) {
                case NNType::None:
                    break;
                case NNType::RGB: {
                    auto nn = createNN(node, pipeline, *rgb);
                    daiNodes.push_back(std::move(nn));
                    break;
                }
                case NNType::Spatial: {
                    RCLCPP_WARN(node->get_logger(), "Spatial NN selected, but configuration is RGBStereo.");
                }
                default:
                    break;
            }
            daiNodes.push_back(std::move(rgb));
            daiNodes.push_back(std::move(left));
            daiNodes.push_back(std::move(right));
            break;
        }
        case PipelineType::Stereo: {
            auto left = std::make_unique<dai_nodes::SensorWrapper>("left", node, pipeline, device, dai::CameraBoardSocket::LEFT);
            auto right = std::make_unique<dai_nodes::SensorWrapper>("right", node, pipeline, device, dai::CameraBoardSocket::RIGHT);
            daiNodes.push_back(std::move(left));
            daiNodes.push_back(std::move(right));
            break;
        }
        case PipelineType::Depth: {
            auto stereo = std::make_unique<dai_nodes::Stereo>("stereo", node, pipeline, device);
            daiNodes.push_back(std::move(stereo));
            break;
        }
        case PipelineType::CamArray: {
            int i = 0;
            int j = 0;
            for(auto& sensor : device->getCameraSensorNames()) {
                // append letter for greater sensor number
                if(i % alphabet.size() == 0) {
                    j++;
                }
                std::string nodeName(j, alphabet[i % alphabet.size()]);
                auto daiNode = std::make_unique<dai_nodes::SensorWrapper>(nodeName, node, pipeline, device, sensor.first);
                daiNodes.push_back(std::move(daiNode));
                i++;
            };
            break;
        }
        case PipelineType::Rae: {
            auto rgb = std::make_unique<dai_nodes::SensorWrapper>("rgb", node, pipeline, device, dai::CameraBoardSocket::RGB);
            auto stereo_front = std::make_unique<dai_nodes::Stereo>(
                "stereo_front", node, pipeline, device, "left_front", "right_front", dai::CameraBoardSocket::CAM_B, dai::CameraBoardSocket::CAM_C);
            auto stereo_back = std::make_unique<dai_nodes::Stereo>(
                "stereo_back", node, pipeline, device, "left_back", "right_back", dai::CameraBoardSocket::CAM_D, dai::CameraBoardSocket::CAM_E);
            daiNodes.push_back(std::move(rgb));
            daiNodes.push_back(std::move(stereo_front));
            daiNodes.push_back(std::move(stereo_back));
            break;
        }
        default: {
            std::string configuration = pipelineType;
            throw std::runtime_error("UNKNOWN PIPELINE TYPE SPECIFIED/CAMERA DOESN'T SUPPORT GIVEN PIPELINE. Configuration: " + configuration);
        }
    }
    if(enableImu) {
        auto imu = std::make_unique<dai_nodes::Imu>("imu", node, pipeline);
        daiNodes.push_back(std::move(imu));
    }

    RCLCPP_INFO(node->get_logger(), "Finished setting up pipeline.");
    return daiNodes;
}
std::unique_ptr<dai_nodes::BaseNode> PipelineGenerator::createNN(rclcpp::Node* node, std::shared_ptr<dai::Pipeline> pipeline, dai_nodes::BaseNode& daiNode) {
    auto nn = std::make_unique<dai_nodes::NNWrapper>("nn", node, pipeline);
    daiNode.link(nn->getInput(), static_cast<int>(dai_nodes::link_types::RGBLinkType::preview));
    return nn;
}
std::unique_ptr<dai_nodes::BaseNode> PipelineGenerator::createSpatialNN(rclcpp::Node* node,
                                                                        std::shared_ptr<dai::Pipeline> pipeline,
                                                                        dai_nodes::BaseNode& daiNode,
                                                                        dai_nodes::BaseNode& daiStereoNode) {
    auto nn = std::make_unique<dai_nodes::SpatialNNWrapper>("nn", node, pipeline);
    daiNode.link(nn->getInput(static_cast<int>(dai_nodes::nn_helpers::link_types::SpatialNNLinkType::input)),
                 static_cast<int>(dai_nodes::link_types::RGBLinkType::preview));
    daiStereoNode.link(nn->getInput(static_cast<int>(dai_nodes::nn_helpers::link_types::SpatialNNLinkType::inputDepth)));
    return nn;
}
PipelineType PipelineGenerator::validatePipeline(rclcpp::Node* node, PipelineType type, int sensorNum) {
    if(sensorNum == 1) {
        if(type != PipelineType::RGB) {
            RCLCPP_ERROR(node->get_logger(), "Wrong pipeline chosen for camera as it has only one sensor. Switching to RGB.");
            return PipelineType::RGB;
        }
    } else if(sensorNum == 2) {
        if(type != PipelineType::Stereo && type != PipelineType::Depth) {
            RCLCPP_ERROR(node->get_logger(), "Wrong pipeline chosen for camera as it has only stereo pair. Switching to Stereo.");
            return PipelineType::Stereo;
        }
    } else if(sensorNum > 3) {
        if(type != PipelineType::Rae && type != PipelineType::CamArray) {
            RCLCPP_ERROR(node->get_logger(), "For cameras with more than three sensors you can only use CamArray. Switching to CamArray.");
            return PipelineType::CamArray;
        }
    }
    return type;
}
}  // namespace pipeline_gen
}  // namespace depthai_ros_driver