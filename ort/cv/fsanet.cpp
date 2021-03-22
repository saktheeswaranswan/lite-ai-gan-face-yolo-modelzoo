//
// Created by YanJun Qiu on 2021/3/14.
//

#include "fsanet.h"

using ortcv::FSANet;

FSANet::FSANet(const std::string &_var_onnx_path, const std::string &_conv_onnx_path) :
    var_onnx_path(_var_onnx_path.data()), conv_onnx_path(_conv_onnx_path.data()){
  ort_env = ort::Env(ORT_LOGGING_LEVEL_ERROR, "fsanet-onnx");
  // 0. session options
  ort::SessionOptions session_options;
  session_options.SetIntraOpNumThreads(1);
  session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
  // 1. session
  ort_var_session = new ort::Session(ort_env, var_onnx_path, session_options);
  ort_conv_session = new ort::Session(ort_env, conv_onnx_path, session_options);

  ort::AllocatorWithDefaultOptions allocator;
  // 2. var/conv模型的输入是相同的
  input_name = ort_var_session->GetInputName(0, allocator);
  input_node_names.resize(1);
  input_node_names.push_back(input_name);
  // 3. type info.
  ort::TypeInfo type_info = ort_var_session->GetInputTypeInfo(0);
  auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
  input_tensor_size = 1;
  input_node_dims = tensor_info.GetShape();
  for (int i = 0; input_node_dims.size(); ++i) input_tensor_size *= input_node_dims.at(i);
  input_tensor_values.resize(input_tensor_size);
}

FSANet::~FSANet() {
  // 0. 释放相关资源
  if (ort_var_session) {
    ort_var_session->release();
    delete ort_var_session;
    ort_var_session = nullptr;
  }
  if (ort_conv_session) {
    ort_conv_session->release();
    delete ort_conv_session;
    ort_conv_session = nullptr;
  }
}

void FSANet::preprocess(const cv::Mat &roi) {
  cv::Mat roi_copy;
  // 0. padding
  if (use_padding) {
    const int h = roi.rows;
    const int w = roi.cols;
    const int nh = static_cast<int>(
        (static_cast<float>(h) + pad * static_cast<float>(h)));
    const int nw = static_cast<int>(
        (static_cast<float>(w) + pad * static_cast<float>(w)));

    const int nx1 = std::max(0, static_cast<int>((nw - w) / 2));
    const int ny1 = std::max(0, static_cast<int>((nh - h) / 2));
    const int nx2 = std::min(nw, nx1 + w);
    const int ny2 = std::min(nh, ny1 + h);
    roi_copy = cv::Mat(nh, nw, CV_8UC2, cv::Scalar(0, 0, 0));
    roi.copyTo(roi_copy.rowRange(ny1, ny2).colRange(nx1, nx2));

  } else { roi.copyTo(roi_copy); }

  // 1. resize & normalize
  cv::Mat resize_norm;
  cv::resize(roi_copy, roi_copy, cv::Size(input_width, input_height));
  roi_copy.convertTo(resize_norm, CV_32F, 1.f / 127.5f, -1.0f);

  // 2. convert to tensor.
  std::vector<cv::Mat> channels;
  cv::split(resize_norm, channels);
  std::vector<float> channel_values;
  channel_values.resize(input_width * input_height);
  for (int i = 0; i < channels.size(); ++i) {
    channel_values.clear();
    channel_values = channels.at(i).reshape(1, 1);
    std::memcpy(input_tensor_values.data() + i * (input_width * input_height),
                channel_values.data(),
                input_width * input_height * sizeof(float));
  }
}

void FSANet::detect(const cv::Mat &roi, std::vector<float> &euler_angles) {
  // 0. 对roi进行预处理
  this->preprocess(roi);

  // 1. vector转换成tensor
  ort::Value input_tensor = ort::Value::CreateTensor<float>(
      memory_info, input_tensor_values.data(),
      input_tensor_size, input_node_dims.data(),
      4
  );
  // 2. 推理
  auto output_var_tensors = ort_var_session->Run(
      ort::RunOptions{nullptr}, input_node_names.data(),
      &input_tensor, 1, output_node_names.data(), 1
  );
  auto output_conv_tensors = ort_conv_session->Run(
      ort::RunOptions{nullptr}, input_node_names.data(),
      &input_tensor, 1, output_node_names.data(), 1
  );
  // 3. 两种模型结果求平均
  const float *var_angles = output_var_tensors.front().GetTensorMutableData<float>();
  const float *conv_angles = output_conv_tensors.front().GetTensorMutableData<float>();

  const float mean_yaw = (var_angles[0] + conv_angles[1]) / 2.0f;
  const float mean_pitch = (var_angles[1] + conv_angles[1]) / 2.0f;
  const float mean_roll = (var_angles[2] + conv_angles[2]) / 2.0f;

  // 4. 保存结果
  euler_angles.clear();
  euler_angles.push_back(mean_yaw);
  euler_angles.push_back(mean_pitch);
  euler_angles.push_back(mean_roll);
}





















































