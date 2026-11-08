#!/usr/bin/env python3
"""Rebuild the background removal model Unisic downloads (birefnet-lite-dcn.onnx).

The onnx-community export of BiRefNet lite (MIT) expands each of the decoder's
20 deformable convolutions (torchvision deform_conv2d, through
deform_conv2d_onnx_exporter) into Pad, four GatherND bilinear samples, a mask
multiply and a final Conv whose stride equals its kernel, over tensors of up to
784 MB. One run then peaks at 6.6 GB and takes ~9 s on ORT 1.31.1. This script
puts one ONNX DeformConv (opset 19) in place of each expansion: same weights,
same masks to 1/255, 1.9 GB and ~3 s. ONNX Runtime has a CPU kernel for it
from 1.25.0 on (docs/dev/packaging.md "Background removal").

    pip install onnx numpy          # onnx 1.23.2 produced the pinned file
    curl -LO https://huggingface.co/onnx-community/BiRefNet_lite-ONNX/resolve/de15b22ba131738a16dff04aab8bdf8dc32e3ac1/onnx/model.onnx
    python3 scripts/birefnet-deformconv.py model.onnx birefnet-lite-dcn.onnx

The output's size and SHA-256 are pinned in src/editor/BackgroundRemover.cpp
(kPins); a changed output means a new release asset and a new pin.
"""
import hashlib
import sys

import numpy as np
import onnx
from onnx import helper, numpy_helper

SOURCE_SHA256 = "5600024376f572a557870a5eb0afb1e5961636bef4e1e22132025467d0f03333"

src, dst = sys.argv[1:3]
with open(src, "rb") as f:
    digest = hashlib.sha256(f.read()).hexdigest()
if digest != SOURCE_SHA256:
    sys.exit(f"{src}: SHA-256 {digest}, expected the onnx-community file {SOURCE_SHA256}")

m = onnx.load(src)
g = m.graph
nodes = {n.name: n for n in g.node}


def attrs(n):
    return {a.name: helper.get_attribute_value(a) for a in n.attribute}


# DeformConv takes the mask as is; torchvision's modulated variant, as BiRefNet
# calls it, scales the modulator's sigmoid by 2.
two = numpy_helper.from_array(np.array(2.0, np.float32), "dcn_two")
g.initializer.append(two)
replace = {}
for p in sorted({n.name.rsplit("/", 2)[0] for n in g.node
                 if n.name.endswith("/atrous_conv/offset_conv/Conv")}):
    off, mod, end = nodes[p + "/offset_conv/Conv"], nodes[p + "/modulator_conv/Conv"], nodes[p + "/Conv"]
    a_off, a_mod, a_end = attrs(off), attrs(mod), attrs(end)
    assert off.input[0] == mod.input[0], p
    assert a_off["kernel_shape"] == a_mod["kernel_shape"], p
    assert a_off.get("strides", [1, 1]) == a_mod.get("strides", [1, 1]), p
    assert a_off.get("dilations", [1, 1]) == [1, 1], p
    k = a_off["kernel_shape"]
    # The exporter's final Conv runs over the sampled grid with stride == kernel.
    assert a_end["kernel_shape"] == k and a_end["strides"] == k, (p, a_end)
    sig, mask = p + "/dcn_sigmoid", p + "/dcn_mask"
    replace[end.name] = [
        helper.make_node("Sigmoid", [mod.output[0]], [sig], name=sig),
        helper.make_node("Mul", [sig, two.name], [mask], name=mask),
        helper.make_node("DeformConv",
                         [off.input[0], end.input[1], off.output[0],
                          end.input[2] if len(end.input) > 2 else "", mask],
                         [end.output[0]], name=p + "/DeformConv",
                         kernel_shape=k, strides=a_off.get("strides", [1, 1]),
                         pads=a_off.get("pads", [0, 0, 0, 0]), dilations=[1, 1],
                         group=1, offset_group=1),
    ]
assert len(replace) == 20, len(replace)

# The new nodes take the final Conv's place, so the graph stays topologically
# sorted; walking back from the outputs then drops the orphaned expansion.
kept = [x for n in g.node for x in replace.get(n.name, [n])]
needed = {o.name for o in g.output}
live = []
for n in reversed(kept):
    if any(o in needed for o in n.output):
        live.append(n)
        needed.update(i for i in n.input if i)
live.reverse()
print(f"{len(replace)} deformable convolutions, nodes {len(g.node)} -> {len(live)}")
del g.node[:]
g.node.extend(live)
used = {i for n in g.node for i in n.input}
inits = [i for i in g.initializer if i.name in used]
del g.initializer[:]
g.initializer.extend(inits)
del g.value_info[:]
# Opset 17 -> 19 for DeformConv. The ops this graph uses only gained
# attributes or types between the two, none changed meaning.
for o in m.opset_import:
    if o.domain in ("", "ai.onnx"):
        o.version = 19
onnx.checker.check_model(m)
onnx.save(m, dst)
with open(dst, "rb") as f:
    data = f.read()
print(f"{dst}: {len(data)} bytes, SHA-256 {hashlib.sha256(data).hexdigest()}")
