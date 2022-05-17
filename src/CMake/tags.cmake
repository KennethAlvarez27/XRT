# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2019-2021 Xilinx, Inc. All rights reserved.
#
add_custom_target(
  tags
  COMMAND ${CTAGS}
  --root ${XRT_SOURCE_DIR}
  --etags
  -f TAGS
  )
