#pragma once
#include <glm/glm.hpp>
namespace lholo::ui::preview {
// Native structure fitting produces GUI-space coordinates in its world
// matrix. Enlarge X/Y around the fixed canvas centre, preserving clip Z/W.
inline glm::mat4 zoomPreviewTransform(glm::mat4 matrix, glm::vec2 center, float zoom) {
    for (int column = 0; column < 4; ++column) {
        matrix[column][0] = matrix[column][0] * zoom + center.x * (1 - zoom) * matrix[column][3];
        matrix[column][1] = matrix[column][1] * zoom + center.y * (1 - zoom) * matrix[column][3];
    }
    return matrix;
}
}
