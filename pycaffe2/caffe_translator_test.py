# This a large test that goes through the translation of the bvlc caffenet
# model, runs an example through the whole model, and verifies numerically
# that all the results look right. In default, it is disabled unless you
# explicitly want to run it.

from caffe2.proto import caffe2_pb2
from caffe.proto import caffe_pb2
from google.protobuf import text_format
import numpy as np
import os
from pycaffe2 import caffe_translator, utils, workspace
import sys
import unittest

class TestNumericalEquivalence(unittest.TestCase):
  def testBlobs(self):
    names = ["conv1", "pool1", "norm1", "conv2", "pool2", "norm2", "conv3",
             "conv4", "conv5", "pool5", "fc6", "fc7", "fc8", "prob"]
    for name in names:
      print 'Verifying ', name
      caffe2_result = workspace.FetchBlob(name)
      reference = np.load(
          'data/testdata/caffe_translator/' + name + '_dump.npy')
      self.assertEqual(caffe2_result.shape, reference.shape)
      scale = np.max(caffe2_result)
      np.testing.assert_almost_equal(caffe2_result / scale, reference / scale,
                                     decimal=5)

if __name__ == '__main__':
  if len(sys.argv) == 1:
    print ('If you do not explicitly ask to run this test, I will not run it. '
           'Pass in any argument to have the test run for you.')
    sys.exit(0)
  if not os.path.exists('data/testdata/caffe_translator'):
    print 'No testdata existing for the caffe translator test. Exiting.'
    sys.exit(0)
  # We will do all the computation stuff in the global space.
  caffenet = caffe_pb2.NetParameter()
  caffenet_pretrained = caffe_pb2.NetParameter()
  text_format.Merge(open('data/testdata/caffe_translator/deploy.prototxt').read(),
                    caffenet)
  caffenet_pretrained.ParseFromString(
      open('data/testdata/caffe_translator/bvlc_reference_caffenet.caffemodel')
          .read())
  net, pretrained_params = caffe_translator.TranslateModel(
      caffenet, caffenet_pretrained)
  caffe_translator.DeleteDropout(net)
  for param in pretrained_params.protos:
      workspace.FeedBlob(param.name, utils.Caffe2TensorToNumpyArray(param))
  # Let's also feed in the data from the Caffe test code.
  data = np.load('data/testdata/caffe_translator/data_dump.npy').astype(np.float32)
  workspace.FeedBlob('data', data)
  # Actually running the test.
  workspace.RunNetOnce(net.SerializeToString())
  unittest.main()