#pragma once

#include <QVector>

#include <cmath>

namespace kg::ml {

/// L2-normalises a vector in place (no-op for a zero vector).
inline void normalise(QVector<float> &vector)
{
    double sum = 0.0;
    for (const float value : vector)
        sum += static_cast<double>(value) * value;
    const double norm = std::sqrt(sum);
    if (norm <= 1e-12)
        return;
    for (float &value : vector)
        value = static_cast<float>(value / norm);
}

/// Cosine similarity of two L2-normalised vectors, which is just their dot
/// product. Zero when the sizes differ (or either is empty) instead of reading
/// out of bounds.
inline float cosineSimilarity(const QVector<float> &a, const QVector<float> &b)
{
    if (a.size() != b.size() || a.isEmpty())
        return 0.0f;
    double dot = 0.0;
    for (int i = 0; i < a.size(); ++i)
        dot += static_cast<double>(a.at(i)) * b.at(i);
    return static_cast<float>(dot);
}

} // namespace kg::ml
