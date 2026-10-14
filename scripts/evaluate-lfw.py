#!/usr/bin/env python3
"""Measure the M3 face-clustering exit criterion on LFW.

The PRD asks whether face clusters are "layak rename manual" (good enough that a
user can rename each cluster to one person). That is answered from two angles,
both from the same `faces.embedding` blobs the app itself wrote:

  clustering   - how pure are the clusters the app produced?
  verification - does the embedding separate identities at all? (official LFW
                 pairs.txt, 10-fold cross-validated threshold)

LFW is the opposite of a personal library (5749 identities over 13233 images,
median 1 image per identity), so `--sweep` also re-runs the app's greedy
clustering per subset of identities with >= K images. As the set becomes
personal-like (few identities, many photos each) the same algorithm scores far
better; both numbers matter and neither alone settles the criterion.

Reproduce:

    mkdir -p /tmp/kg-lfw/data/kin-galeri/models
    cp build-tools/models/*.onnx /tmp/kg-lfw/data/kin-galeri/models/
    XDG_DATA_HOME=/tmp/kg-lfw/data build-ml/kin-galeri --no-ui --no-home-scan \
        --db /tmp/kg-lfw/lfw.db --cache /tmp/kg-lfw/cache --index /path/to/lfw
    python3 scripts/evaluate-lfw.py cluster  /tmp/kg-lfw/lfw.db /path/to/lfw
    python3 scripts/evaluate-lfw.py verify   /tmp/kg-lfw/lfw.db /path/to/lfw /path/to/pairs.txt
    python3 scripts/evaluate-lfw.py sweep    /tmp/kg-lfw/lfw.db /path/to/lfw 1 2 3 5 8 10 20 30

Faces must be enabled in the config (`[smart] enabled=true`, `faces=true`,
`auto_analyze=true`) and `pairs.txt` comes with the LFW archive.
"""
import collections
import math
import os
import sqlite3
import struct
import sys

# FacePipelineOptions::clusterThreshold - keep in sync with FacePipeline.h.
THRESHOLD = 0.40


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def normalise(v):
    n = sum(x * x for x in v) ** 0.5
    return [x / n for x in v] if n > 1e-12 else v


def open_db(db_path):
    con = sqlite3.connect(db_path)
    con.row_factory = sqlite3.Row
    return con


Face = collections.namedtuple("Face", "id media_id label emb confidence")


def best_face_per_image(con, lfw_root):
    """One identity per image, in the order FacePipeline clusters them.

    LFW images are tightly cropped portraits, so a second detection is almost
    always the same face; keeping the most confident one is enough to score
    identity clustering. Order is confidence DESC, id ASC - the same ORDER BY
    the pipeline uses, which matters because the clustering is greedy.
    """
    labels = {
        r["id"]: os.path.relpath(r["path"], lfw_root).split(os.sep)[0]
        for r in con.execute("SELECT id, path FROM media WHERE deleted_at IS NULL")
    }
    seen = {}
    for r in con.execute(
        "SELECT id, media_id, embedding, confidence FROM faces "
        "WHERE embedding IS NOT NULL ORDER BY confidence DESC, id ASC"
    ):
        if r["media_id"] in seen:
            continue
        emb = struct.unpack(f"<{len(r['embedding']) // 4}f", r["embedding"])
        seen[r["media_id"]] = Face(r["id"], r["media_id"], labels[r["media_id"]],
                                   list(emb), r["confidence"])
    return labels, sorted(seen.values(), key=lambda f: (-f.confidence, f.id))


def embed_for_files(con, lfw_root):
    """basename -> embedding, for the pairs lookup."""
    faces = best_face_per_image(con, lfw_root)[1]
    emb_of_media = {f.media_id: f.emb for f in faces}
    out = {}
    for r in con.execute("SELECT id, path FROM media WHERE deleted_at IS NULL"):
        if r["id"] in emb_of_media:
            out[os.path.basename(r["path"])] = emb_of_media[r["id"]]
    return out


def greedy_cluster(faces):
    """Replicates FacePipeline::run() step 3: assign to the best running-mean
    centroid with cosine > threshold, else start a new cluster."""
    clusters = []  # [centroid, Counter(label -> n), size]
    for face in faces:
        emb = face.emb
        label = face.label
        best, best_score = -1, THRESHOLD
        for i, (centroid, _, _) in enumerate(clusters):
            score = dot(emb, centroid)
            if score > best_score:
                best, best_score = i, score
        if best < 0:
            clusters.append([list(emb), collections.Counter({label: 1}), 1])
            continue
        centroid, counts, size = clusters[best]
        for k in range(len(centroid)):
            centroid[k] = (centroid[k] * size + emb[k]) / (size + 1)
        centroid[:] = normalise(centroid)
        counts[label] += 1
        clusters[best][2] = size + 1
    return clusters


def cluster_stats(clusters, n_faces):
    same = pairs = faces_in_pure = 0
    for _centroid, counts, size in clusters:
        pairs += size * (size - 1) // 2
        same += sum(c * (c - 1) // 2 for c in counts.values())
        if len(counts) == 1:
            faces_in_pure += size
    purity = sum(c.most_common(1)[0][1] for _, c, _ in clusters) / n_faces
    precision = same / pairs if pairs else 0.0
    return purity, precision, same, faces_in_pure


def cmd_cluster(db_path, lfw_root):
    con = open_db(db_path)
    total = con.execute("SELECT COUNT(*) FROM media WHERE deleted_at IS NULL").fetchone()[0]
    _labels, faces = best_face_per_image(con, lfw_root)
    clusters = greedy_cluster(faces)
    purity, precision, same, faces_in_pure = cluster_stats(clusters, len(faces))
    true_counts = collections.Counter(f.label for f in faces)
    true_pairs = sum(c * (c - 1) // 2 for c in true_counts.values())
    recall = same / true_pairs if true_pairs else 0.0
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    singletons = sum(1 for _, _, s in clusters if s == 1)

    print("== Clustering (what the exit criterion asks about) ==")
    print(f"images indexed                     : {total}")
    print(f"images with a detected face        : {len(faces)} ({100*len(faces)/total:.1f}%)")
    print(f"true identities                    : {len(true_counts)}")
    print(f"predicted clusters                 : {len(clusters)}")
    print(f"purity (fixed identity per cluster): {100*purity:.2f}%")
    print(f"faces in a single-identity cluster : {faces_in_pure} ({100*faces_in_pure/len(faces):.1f}%)")
    print(f"pairwise precision / recall / F1   : {100*precision:.2f}% / {100*recall:.2f}% / {100*f1:.2f}%")
    print(f"singletons (nothing to rename)     : {singletons} ({100*singletons/len(clusters):.1f}% of clusters)")
    worst = sorted(
        ((sum(c.values()), c.most_common(1)[0][1], len(c), [l for l, _ in c.most_common(3)])
         for _, c, _ in clusters if len(c) > 1),
        reverse=True,
    )
    print("worst mixed clusters (size / majority / #identities):")
    for size, top, nlabels, top3 in worst[:5]:
        print(f"    size {size:5d}  majority {top:5d}  identities {nlabels:3d}  e.g. {top3}")


def cmd_verify(db_path, lfw_root, pairs_path):
    emb = embed_for_files(open_db(db_path), lfw_root)
    lines = open(pairs_path).read().split("\n")
    folds = int(lines[0].split()[0])
    pairs = []
    for line in lines[1:]:
        parts = line.split()
        if len(parts) == 3:
            name, a, b = parts
            pairs.append((1, f"{name}_{int(a):04d}.jpg", f"{name}_{int(b):04d}.jpg"))
        elif len(parts) == 4:
            n1, a, n2, b = parts
            pairs.append((0, f"{n1}_{int(a):04d}.jpg", f"{n2}_{int(b):04d}.jpg"))

    usable = [(lab, dot(emb[f1], emb[f2])) for lab, f1, f2 in pairs if f1 in emb and f2 in emb]
    n_matched = sum(1 for lab, _ in usable if lab == 1)
    n_mismatched = len(usable) - n_matched

    def acc_at(th, data):
        return sum(1 for lab, s in data if (s >= th) == (lab == 1)) / len(data)

    steps = (t / 1000 for t in range(0, 1001))
    best_th, best_acc = max(((t, acc_at(t, usable)) for t in steps), key=lambda kv: kv[1])
    per = len(usable) // folds
    fold_acc = []
    for k in range(folds):
        test = usable[k * per:(k + 1) * per]
        train = usable[:k * per] + usable[(k + 1) * per:]
        th = max(((t, acc_at(t, train)) for t in (x / 1000 for x in range(0, 1001))),
                 key=lambda kv: kv[1])[0]
        fold_acc.append(acc_at(th, test))
    mean = sum(fold_acc) / len(fold_acc)
    std = math.sqrt(sum((a - mean) ** 2 for a in fold_acc) / len(fold_acc))

    print("== Verification (official LFW pairs.txt) ==")
    print(f"pairs defined / usable             : {len(pairs)} / {len(usable)}")
    print(f"accuracy at pipeline threshold     : {100*acc_at(THRESHOLD, usable):.2f}%  (cos >= {THRESHOLD})")
    print(f"best global threshold              : {100*best_acc:.2f}%  (cos >= {best_th:.3f})")
    print(f"10-fold cross-validated accuracy   : {100*mean:.2f}% +/- {100*std:.2f}%")
    print(f"per fold                           : {[round(100*a, 2) for a in fold_acc]}")
    fp = sum(1 for lab, s in usable if lab == 0 and s >= THRESHOLD)
    fn = sum(1 for lab, s in usable if lab == 1 and s < THRESHOLD)
    print(f"at pipeline threshold: false accepts {100*fp/max(1,n_mismatched):.2f}%, "
          f"false rejects {100*fn/max(1,n_matched):.2f}%")


def cmd_sweep(db_path, lfw_root, ks):
    _labels, all_faces = best_face_per_image(open_db(db_path), lfw_root)
    per_id = collections.Counter(f.label for f in all_faces)
    header = (f"{'K':>4} {'identities':>10} {'images':>7} {'clusters':>8} {'purity':>7} "
              f"{'pairF1':>7} {'faces pure':>10} {'singletons':>10}")
    print(f"full set: {len(all_faces)} images, {len(per_id)} identities")
    print(header)
    print("-" * len(header))
    for k in ks:
        ids = {i for i, c in per_id.items() if c >= k}
        subset = [f for f in all_faces if f.label in ids]
        clusters = greedy_cluster(subset)
        purity, precision, same, faces_in_pure = cluster_stats(clusters, len(subset))
        true_pairs = sum(c * (c - 1) // 2 for i, c in per_id.items() if i in ids)
        recall = same / true_pairs if true_pairs else 0.0
        f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
        singletons = sum(1 for _, _, s in clusters if s == 1)
        print(f"{k:>4} {len(ids):>10} {len(subset):>7} {len(clusters):>8} {100*purity:>6.1f}% "
              f"{100*f1:>6.1f}% {100*faces_in_pure/len(subset):>9.1f}% {100*singletons/len(clusters):>9.1f}%")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(2)
    mode, db, root = sys.argv[1], sys.argv[2], sys.argv[3]
    if mode == "cluster":
        cmd_cluster(db, root)
    elif mode == "verify":
        cmd_verify(db, root, sys.argv[4])
    elif mode == "sweep":
        cmd_sweep(db, root, [int(x) for x in sys.argv[4:]] or [1, 2, 3, 5, 10, 20, 30])
    else:
        print(__doc__)
        sys.exit(2)
