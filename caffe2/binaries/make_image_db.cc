// This script converts an image dataset to a database.
//
// FLAGS_input_folder is the root folder that holds all the images, and
// FLAGS_list_file should be a list of files as well as their labels, in the
// format as
//   subfolder1/file1.JPEG 7
//   ....

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <fstream>  // NOLINT(readability/streams)
#include <random>
#include <string>

#include "caffe2/core/common.h"
#include "caffe2/core/db.h"
#include "caffe2/proto/caffe2.pb.h"
#include "caffe2/binaries/gflags_namespace.h"
#include "glog/logging.h"

DEFINE_bool(shuffle, false,
    "Randomly shuffle the order of images and their labels");
DEFINE_string(input_folder, "", "The input image file name.");
DEFINE_string(list_file, "", "The text file containing the list of images.");
DEFINE_string(output_db_name, "", "The output training leveldb name.");
DEFINE_string(db, "leveldb", "The db type.");
DEFINE_bool(raw, false,
    "If set, we pre-read the images and store the raw buffer.");
DEFINE_bool(color, true, "If set, load images in color.");
DEFINE_int32(scale, 256,
    "If FLAGS_raw is set, scale all the images' shorter edge to the given "
    "value.");
DEFINE_bool(warp, false, "If warp is set, warp the images to square.");


namespace caffe2 {

void ConvertImageDataset(
    const string& input_folder, const string& list_filename,
    const string& output_db_name, const bool shuffle) {
  std::ifstream list_file(list_filename);
  std::vector<std::pair<std::string, int> > lines;
  std::string filename;
  int file_label;
  while (list_file >> filename >> file_label) {
    lines.push_back(std::make_pair(filename, file_label));
  }
  if (FLAGS_shuffle) {
    // randomly shuffle data
    LOG(INFO) << "Shuffling data";
    std::shuffle(lines.begin(), lines.end(),
                 std::default_random_engine(1701));
  }
  LOG(INFO) << "A total of " << lines.size() << " images.";


  LOG(INFO) << "Opening db " << output_db_name;
  std::unique_ptr<db::DB> db(db::CreateDB(FLAGS_db, output_db_name, db::NEW));
  std::unique_ptr<db::Transaction> transaction(db->NewTransaction());

  TensorProtos protos;
  TensorProto* data = protos.add_protos();
  TensorProto* label = protos.add_protos();
  if (FLAGS_raw) {
    data->set_data_type(TensorProto::BYTE);
    data->add_dims(0);
    data->add_dims(0);
    if (FLAGS_color) {
      data->add_dims(3);
    }
  } else {
    data->set_data_type(TensorProto::STRING);
    data->add_dims(1);
    data->add_string_data("");
  }
  label->set_data_type(TensorProto::INT32);
  label->add_dims(1);
  label->add_int32_data(0);
  const int kMaxKeyLength = 256;
  char key_cstr[kMaxKeyLength];
  string value;
  int count = 0;

  for (int item_id = 0; item_id < lines.size(); ++item_id) {
    // First, set label.
    label->set_int32_data(0, lines[item_id].second);
    if (!FLAGS_raw) {
      // Second, read images.
      std::ifstream image_file_stream(input_folder + lines[item_id].first);
      if (!image_file_stream) {
        LOG(ERROR) << "Cannot open " << input_folder << lines[item_id].first
                   << ". Skipping.";
      } else {
        data->mutable_string_data(0)->assign(
            (std::istreambuf_iterator<char>(image_file_stream)),
            std::istreambuf_iterator<char>());
      }
    } else {
      // Need to do some opencv magic.
      cv::Mat img = cv::imread(
          input_folder + lines[item_id].first,
          FLAGS_color ? CV_LOAD_IMAGE_COLOR : CV_LOAD_IMAGE_GRAYSCALE);
      // Do resizing.
      cv::Mat resized_img;
      int scaled_width, scaled_height;
      if (FLAGS_warp) {
        scaled_width = FLAGS_scale;
        scaled_height = FLAGS_scale;
      } else if (img.rows > img.cols) {
        scaled_width = FLAGS_scale;
        scaled_height = static_cast<float>(img.rows) * FLAGS_scale / img.cols;
      } else {
        scaled_height = FLAGS_scale;
        scaled_width = static_cast<float>(img.cols) * FLAGS_scale / img.rows;
      }
      cv::resize(img, resized_img, cv::Size(scaled_width, scaled_height), 0, 0,
                   cv::INTER_LINEAR);
      data->set_dims(0, scaled_height);
      data->set_dims(1, scaled_width);
      DCHECK(resized_img.isContinuous());
      data->set_byte_data(
          resized_img.ptr(),
          scaled_height * scaled_width * (FLAGS_color ? 3 : 1));
    }
    snprintf(key_cstr, kMaxKeyLength, "%08d_%s", item_id,
             lines[item_id].first.c_str());
    protos.SerializeToString(&value);
    // Put in db
    transaction->Put(string(key_cstr), value);
    if (++count % 1000 == 0) {
      // Commit the current writes.
      transaction->Commit();
      LOG(INFO) << "Processed " << count << " files.";
    }
  }
  LOG(INFO) << "Processed a total of " << count << " files.";
}

}  // namespace caffe2


int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  gflags::SetUsageMessage("Converts an image dataset to a db.");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  caffe2::ConvertImageDataset(
      FLAGS_input_folder, FLAGS_list_file,
      FLAGS_output_db_name, FLAGS_shuffle);
  return 0;
}
