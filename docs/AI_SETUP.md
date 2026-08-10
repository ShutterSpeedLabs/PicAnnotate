# AI setup

PicAnnotate's model-assisted features — YOLO detection and segmentation, SAM
click-to-segment, and CLIP zero-shot classification — need two things that are
not in the repository: **ONNX Runtime** (a build dependency) and the **model
files** themselves (a runtime dependency).

Neither is required to build or use the rest of the app. Without ONNX Runtime
the project still compiles; SAM and CLIP report that they are unavailable, and
YOLO keeps working through OpenCV's DNN module. Without a given model file, that
model shows as *Not installed* in the Model Manager and everything else carries
on.

---

## 1. ONNX Runtime

### Why it is a separate dependency

OpenCV 4.9's `cv::dnn` is already linked and runs YOLO well. It does not run
SAM: the mask decoder relies on dynamic shapes and on operators (`GridSample`,
`ScatterND`) that `cv::dnn` either does not implement or implements
differently. Rather than restrict SAM to whichever export `cv::dnn` tolerates,
those models go through ONNX Runtime, which also opens the door to GPU
execution via DirectML or CUDA.

### Install

1. Go to <https://github.com/microsoft/onnxruntime/releases> and pick a release
   (1.20.x or newer is fine).
2. Download the Windows x64 archive:
   - `onnxruntime-win-x64-<version>.zip` — CPU only, about 30 MB. **Start here.**
   - `onnxruntime-win-x64-directml-<version>.zip` — adds the DirectML provider,
     which will use any DX12 GPU including Intel integrated graphics.
   - `onnxruntime-win-x64-gpu-<version>.zip` — CUDA, NVIDIA only, and needs a
     matching CUDA + cuDNN installation.
3. Unzip it so that this path exists:

   ```
   C:\onnxruntime\include\onnxruntime_cxx_api.h
   C:\onnxruntime\lib\onnxruntime.lib
   C:\onnxruntime\lib\onnxruntime.dll
   ```

   The archive unpacks to a versioned folder; either rename it to
   `C:\onnxruntime` or keep the name and tell qmake where it is (below).

4. Re-run qmake. `PicAnnotate.pro` probes for the SDK and prints what it found:

   ```
   Project MESSAGE: ONNX Runtime found at C:/onnxruntime — SAM and CLIP enabled.
   ```

   If it prints *not found*, the path is wrong. `C:/onnxruntime` is only the
   default — override it either way:

   ```bash
   qmake ONNXRUNTIME_DIR=D:/sdk/onnxruntime-win-x64-1.20.1
   ```

   or set an `ONNXRUNTIME_DIR` environment variable, which Qt Creator will pick
   up as well.

The build copies `onnxruntime.dll` (and `DirectML.dll` when present) next to the
executable automatically, so nothing needs to go on `PATH`.

### Choosing a provider

Model Manager → **Run models on**. The choice is a request, not a guarantee: if
the installed runtime has no DirectML or CUDA provider, models load on CPU and
say so. There is no NVIDIA GPU on a typical Intel laptop, so **CPU** and
**DirectML** are the two realistic options there.

---

## 2. Model files

### Where they are looked for

In this order:

1. A path you pinned by hand (Model Manager → *Locate file...*).
2. `<application folder>\models\` — where the build drops bundled models.
3. `%LOCALAPPDATA%\ShutterSpeedLabs\PicAnnotate\models\` — where downloads land.
   Model Manager → *Open models folder* opens this one.

The file name matters: each catalogue entry expects a specific name (shown in
the Model Manager's details pane). Either name your file to match or pin it with
*Locate file...*.

### Why most entries have no download button

The catalogue in `resources/ai/models.json` ships **no download URLs**. These
models are published as PyTorch checkpoints, and there is no canonical,
permanent ONNX build of any of them to link to — a URL baked into the app would
rot, and a wrong checksum would be worse than none. Each entry instead carries
the exact command that produces the file.

The download machinery is fully implemented and used the moment a URL exists.
To use it: Model Manager → *Add model...*, give an id matching a built-in entry
(this replaces it), and paste a URL and SHA-256 you trust. Downloads are
streamed to disk, hashed as they arrive, and discarded on a checksum mismatch.

---

## 3. Exporting the models

### YOLO — detection and segmentation

```bash
pip install ultralytics
yolo export model=yolov8n-seg.pt format=onnx opset=12 imgsz=640 simplify=True
```

Swap `yolov8n-seg` for any of `yolov8n`, `yolov8s`, `yolov8m`, `yolo11n`,
`yolov8s-seg`, `yolo11n-seg`. The `.pt` checkpoint downloads on first use.

`opset=12` is not optional — later opsets emit operators `cv::dnn` reads
differently, and the symptom is boxes in the wrong places rather than a load
error. `simplify=True` folds away shape arithmetic that otherwise trips the
same parser.

Copy the resulting `.onnx` into the models folder.

> **Licensing.** Ultralytics YOLOv8/YOLO11 weights are AGPL-3.0. Using them to
> label your own data is fine. Redistributing them inside a product, or shipping
> a product whose output depends on them, carries obligations — which is one
> reason nothing is bundled here.

### SAM — MobileSAM and SAM ViT-B

```bash
pip install samexporter
```

**MobileSAM** (about 40 MB total, the practical choice on CPU) — get
`mobile_sam.pt` from <https://github.com/ChaoningZhang/MobileSAM>:

```bash
python -m samexporter.export_encoder --checkpoint mobile_sam.pt \
    --output mobile-sam-encoder.onnx --model-type vit_t
python -m samexporter.export_decoder --checkpoint mobile_sam.pt \
    --output mobile-sam-decoder.onnx --model-type vit_t --return-single-mask
```

**SAM ViT-B** (sharper, several times slower) — get `sam_vit_b_01ec64.pth` from
<https://github.com/facebookresearch/segment-anything>:

```bash
python -m samexporter.export_encoder --checkpoint sam_vit_b_01ec64.pth \
    --output sam-vit-b-encoder.onnx --model-type vit_b
python -m samexporter.export_decoder --checkpoint sam_vit_b_01ec64.pth \
    --output sam-vit-b-decoder.onnx --model-type vit_b --return-single-mask
```

Check `--help` if a flag has moved; samexporter's CLI does change between
releases.

Two things to watch:

- **The encoder and decoder must come from the same export.** Mixing a MobileSAM
  encoder with a ViT-B decoder does not error — the shapes line up — it just
  produces nonsense masks. The catalogue records the pairing and the app warns
  when the selected pair disagrees.
- Decoder exports vary in whether they take an `orig_im_size` input and whether
  they emit one mask or three. PicAnnotate inspects the graph's actual input and
  output names at load time and adapts, so both common variants work.

### CLIP — zero-shot classification

CLIP is exported as two separate towers so that text embeddings for your class
names can be computed once and cached, instead of on every region.

```bash
pip install torch transformers
```

```python
import torch
from transformers import CLIPModel

model = CLIPModel.from_pretrained("openai/clip-vit-base-patch32").eval()

class ImageTower(torch.nn.Module):
    def __init__(self, m):
        super().__init__()
        self.m = m
    def forward(self, pixel_values):
        f = self.m.get_image_features(pixel_values=pixel_values)
        return f / f.norm(dim=-1, keepdim=True)

class TextTower(torch.nn.Module):
    def __init__(self, m):
        super().__init__()
        self.m = m
    def forward(self, input_ids, attention_mask):
        f = self.m.get_text_features(input_ids=input_ids, attention_mask=attention_mask)
        return f / f.norm(dim=-1, keepdim=True)

torch.onnx.export(
    ImageTower(model),
    torch.randn(1, 3, 224, 224),
    "clip-vit-b32-image.onnx",
    input_names=["pixel_values"],
    output_names=["image_embeds"],
    dynamic_axes={"pixel_values": {0: "batch"}, "image_embeds": {0: "batch"}},
    opset_version=14,
)

torch.onnx.export(
    TextTower(model),
    (torch.ones(1, 77, dtype=torch.long), torch.ones(1, 77, dtype=torch.long)),
    "clip-vit-b32-text.onnx",
    input_names=["input_ids", "attention_mask"],
    output_names=["text_embeds"],
    dynamic_axes={"input_ids": {0: "batch"},
                  "attention_mask": {0: "batch"},
                  "text_embeds": {0: "batch"}},
    opset_version=14,
)
```

Both towers L2-normalise inside the graph, so comparing two embeddings is a
plain dot product.

The text tower needs a tokenizer. Download these two small files from
<https://huggingface.co/openai/clip-vit-base-patch32/tree/main> into the same
models folder:

```
vocab.json
merges.txt
```

PicAnnotate implements CLIP's byte-pair encoding against them directly; no
Python is involved at runtime.

---

## 4. Checking it worked

Open **Tools → Model Manager**. The banner at the top states whether ONNX
Runtime was compiled in and which version. Each model shows one of:

| Status | Meaning |
| --- | --- |
| **Ready** | File found and its runtime is in this build. |
| **Runtime missing** | The file is there, but it needs ONNX Runtime and this build has none. Re-downloading will not help — rebuild with the SDK. |
| **Not installed** | No file found. The details pane shows every folder searched and the command that produces the file. |

Select a model and press **Use for this task** to make it the one the canvas and
the batch runner use.

---

## 5. Using the features

### Building a dataset from raw media

**File → Create Dataset from Videos/Folders...**

Add any number of videos and image folders, choose how densely to sample them,
and the wizard writes the selected frames into `<output>/images/` and opens that
folder as a project.

Sampling is the part worth thinking about. Consecutive video frames are nearly
identical, so taking all of them multiplies the labelling effort without adding
information:

| Control | What it does |
| --- | --- |
| **Every Nth frame** | Fixed stride. Simple and predictable. |
| **Target frame rate** | Stride derived from each video's own rate, so 30 fps and 60 fps footage are sampled comparably. |
| **Max per source** | Stops one long video from dominating a multi-source dataset. |
| **Skip blurred frames** | Rejects frames whose Laplacian variance is below the threshold — mostly motion blur. Test the threshold on a short clip first; the right value depends on the footage. |
| **Skip near-duplicates** | Rejects a frame too similar to the last one *kept*, so a slow pan cannot creep through one small step at a time. |

Frames are named `<source>_<index>.<ext>`, so two sources never collide and the
original order is recoverable.

Tick **Run the detection model once the dataset opens** to go straight into a
batch detection pass when the wizard finishes.

### Detection and segmentation

**AI → Run Model on This Frame** (`Ctrl+D`) or **on All Frames**
(`Ctrl+Shift+D`), or the buttons in the **Predictions** panel.

Results arrive as *predictions*, not annotations: dashed outlines with a
confidence, held in a separate layer that the project never saves. Nothing
touches your annotations until you accept it.

In the Predictions panel:

- **Min score** hides everything below the threshold, across every frame at once.
- **Accept** / **Reject** act on the selected prediction.
- **Accept frame** / **Accept everywhere** commit in bulk, each as a single undo step.
- **Create missing classes on accept** decides what happens when a detection's
  class (e.g. COCO's `person`) has no counterpart in your label schema. On, it
  creates one; off, that prediction is skipped rather than accepted unlabelled.

A batch run over a long video is minutes of CPU. It runs on a worker thread and
can be cancelled; the frame currently being processed finishes first.

### Click-to-segment with SAM

Press **S** or pick **SAM Segment** from the toolbar.

The frame is encoded once — a second or so for MobileSAM, longer for ViT-B —
and the status bar says when it is ready. After that each click is a fast
decoder-only pass:

| Input | Effect |
| --- | --- |
| **Left click** | Include this region |
| **Shift+click** or **right click** | Exclude this region |
| **Drag** | Box prompt |
| **Backspace** | Undo the last prompt |
| **Enter** or **double-click** | Accept the previewed mask as a polygon |
| **Esc** | Clear the prompt |

The accepted polygon takes whichever class is selected in the Labels panel, so
select one first.

Moving to another frame discards the embedding and starts encoding the new one,
which is why SAM is best used to work through one frame at a time.

### Zero-shot classification with CLIP

**AI → Classify Selected Shape** (`Ctrl+K`), or **Classify Unlabelled Shapes on
This Frame**.

This scores a region against **your own class names** — whatever is in the
Labels panel — and assigns the best match. No training and no fixed category
list, so `cracked_insulator` works as well as `cat` does.

Two things affect how well it works:

- **Class names are prompts.** CLIP reads them as English. `damaged rail
  fastener` will do better than `cls_3` or `DMG_RF`.
- **Confidence gate.** A region CLIP is not confident about is left alone rather
  than guessed at, and reported as skipped.

Class embeddings are computed once and reused; renaming or adding a class
recomputes them on the next run.

---

## 6. Troubleshooting

**qmake keeps saying ONNX Runtime is not found.**
It checks for `$$ONNXRUNTIME_DIR/include/onnxruntime_cxx_api.h` specifically.
Confirm that exact file exists. Qt Creator caches qmake output — run *Build →
Run qmake* explicitly after changing the path.

**The app builds but exits immediately on launch.**
`onnxruntime.dll` is missing next to the executable. The post-link step copies
it, but only if it was present at `$$ONNXRUNTIME_DIR/lib/onnxruntime.dll` when
qmake ran. Copy it by hand or re-run qmake.

**A YOLO model loads but the boxes are in the wrong places.**
Almost always an opset mismatch. Re-export with `opset=12 simplify=True`.

**SAM masks are noise.**
The encoder and decoder are from different exports or different model types.
Check that both entries in the Model Manager name each other as their pair.
