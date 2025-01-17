// This script converts the MNIST dataset to leveldb.
// The MNIST dataset could be downloaded at
//    http://yann.lecun.com/exdb/mnist/

#include <fstream>  // NOLINT(readability/streams)
#include <string>

#include "caffe2/core/common.h"
#include "caffe2/core/db.h"
#include "caffe2/proto/caffe2.pb.h"
#include "caffe2/binaries/gflags_namespace.h"
#include "glog/logging.h"

DEFINE_string(image_file, "", "The input image file name.");
DEFINE_string(label_file, "", "The label file name.");
DEFINE_string(output_file, "", "The output db name.");
DEFINE_string(db, "leveldb", "The db type.");
DEFINE_int32(data_limit, -1,
             "If set, only output this number of data points.");
DEFINE_bool(channel_first, false,
            "If set, write the data as channel-first (CHW order) as the old "
            "Caffe does.");

namespace caffe2 {
uint32_t swap_endian(uint32_t val) {
    val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF);
    return (val << 16) | (val >> 16);
}

void convert_dataset(const char* image_filename, const char* label_filename,
        const char* db_path, const int data_limit) {
  // Open files
  std::ifstream image_file(image_filename, std::ios::in | std::ios::binary);
  std::ifstream label_file(label_filename, std::ios::in | std::ios::binary);
  CHECK(image_file) << "Unable to open file " << image_filename;
  CHECK(label_file) << "Unable to open file " << label_filename;
  // Read the magic and the meta data
  uint32_t magic;
  uint32_t num_items;
  uint32_t num_labels;
  uint32_t rows;
  uint32_t cols;

  image_file.read(reinterpret_cast<char*>(&magic), 4);
  magic = swap_endian(magic);
  CHECK_EQ(magic, 2051) << "Incorrect image file magic.";
  label_file.read(reinterpret_cast<char*>(&magic), 4);
  magic = swap_endian(magic);
  CHECK_EQ(magic, 2049) << "Incorrect label file magic.";
  image_file.read(reinterpret_cast<char*>(&num_items), 4);
  num_items = swap_endian(num_items);
  label_file.read(reinterpret_cast<char*>(&num_labels), 4);
  num_labels = swap_endian(num_labels);
  CHECK_EQ(num_items, num_labels);
  image_file.read(reinterpret_cast<char*>(&rows), 4);
  rows = swap_endian(rows);
  image_file.read(reinterpret_cast<char*>(&cols), 4);
  cols = swap_endian(cols);

  // leveldb
  std::unique_ptr<db::DB> mnist_db(db::CreateDB(FLAGS_db, db_path, db::NEW));
  std::unique_ptr<db::Transaction> transaction(mnist_db->NewTransaction());
  // Storing to db
  char label_value;
  std::vector<char> pixels(rows * cols);
  int count = 0;
  const int kMaxKeyLength = 10;
  char key_cstr[kMaxKeyLength];
  string value;

  TensorProtos protos;
  TensorProto* data = protos.add_protos();
  TensorProto* label = protos.add_protos();
  data->set_data_type(TensorProto::BYTE);
  if (FLAGS_channel_first) {
    data->add_dims(1);
    data->add_dims(1);
    data->add_dims(rows);
    data->add_dims(cols);
  } else {
    data->add_dims(1);
    data->add_dims(rows);
    data->add_dims(cols);
    data->add_dims(1);
  }
  label->set_data_type(TensorProto::INT32);
  label->add_dims(1);
  label->add_int32_data(0);

  LOG(INFO) << "A total of " << num_items << " items.";
  LOG(INFO) << "Rows: " << rows << " Cols: " << cols;
  for (int item_id = 0; item_id < num_items; ++item_id) {
    image_file.read(pixels.data(), rows * cols);
    label_file.read(&label_value, 1);
    for (int i = 0; i < rows * cols; ++i) {
      data->set_byte_data(pixels.data(), rows * cols);
    }
    label->set_int32_data(0, static_cast<int>(label_value));
    snprintf(key_cstr, kMaxKeyLength, "%08d", item_id);
    protos.SerializeToString(&value);
    string keystr(key_cstr);

    // Put in db
    transaction->Put(keystr, value);
    if (++count % 1000 == 0) {
      transaction->Commit();
    }
    if (data_limit > 0 && count == data_limit) {
      LOG(INFO) << "Reached data limit of " << data_limit << ", stop.";
      break;
    }
  }
}
}  // namespace caffe2

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  gflags::SetUsageMessage("Converts the raw mnist dataset to a leveldb.");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  caffe2::convert_dataset(FLAGS_image_file.c_str(), FLAGS_label_file.c_str(),
                          FLAGS_output_file.c_str(), FLAGS_data_limit);
  return 0;
}
