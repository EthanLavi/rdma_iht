# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: experiment.proto
"""Generated protocol buffer code."""
from google.protobuf.internal import builder as _builder
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()




DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n\x10\x65xperiment.proto\x12\x08messages\"\n\n\x08\x41\x63kProto\"\xb4\x02\n\x10\x45xperimentParams\x12\x15\n\nthink_time\x18\x01 \x02(\x05:\x01\x30\x12\x1b\n\x0fqps_sample_rate\x18\x02 \x02(\x05:\x02\x31\x30\x12\x1a\n\x0emax_qps_second\x18\x03 \x02(\x05:\x02-1\x12\x13\n\x07runtime\x18\x04 \x02(\x05:\x02\x31\x30\x12\x1f\n\x10unlimited_stream\x18\x05 \x02(\x08:\x05\x66\x61lse\x12\x17\n\x08op_count\x18\x06 \x02(\x05:\x05\x31\x30\x30\x30\x30\x12\x14\n\x08\x63ontains\x18\x07 \x02(\x05:\x02\x38\x30\x12\x12\n\x06insert\x18\x08 \x02(\x05:\x02\x31\x30\x12\x12\n\x06remove\x18\t \x02(\x05:\x02\x31\x30\x12\x11\n\x06key_lb\x18\n \x02(\x05:\x01\x30\x12\x17\n\x06key_ub\x18\x0b \x02(\x05:\x07\x31\x30\x30\x30\x30\x30\x30\x12\x17\n\x0bregion_size\x18\x0c \x02(\x05:\x02\x32\x32')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'experiment_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:

  DESCRIPTOR._options = None
  _globals['_ACKPROTO']._serialized_start=30
  _globals['_ACKPROTO']._serialized_end=40
  _globals['_EXPERIMENTPARAMS']._serialized_start=43
  _globals['_EXPERIMENTPARAMS']._serialized_end=351
# @@protoc_insertion_point(module_scope)
