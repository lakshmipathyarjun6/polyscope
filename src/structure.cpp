// Copyright 2017-2019, Nicholas Sharp and the Polyscope contributors. http://polyscope.run.
#include "polyscope/structure.h"

#include "polyscope/polyscope.h"

#include "imgui.h"

namespace polyscope {

Structure::Structure(std::string name_, std::string subtypeName)
    : name(name_), enabled(subtypeName + "#" + name + "#enabled", true),
      objectTransform(subtypeName + "#" + name + "#object_transform", glm::mat4(1.0)),
      transparency(subtypeName + "#" + name + "#transparency", 1.0),
      transformGizmo(subtypeName + "#" + name + "#transform_gizmo", objectTransform.get(), &objectTransform),
      ignoredSlicePlaneNames(subtypeName + "#" + name + "#ignored_slice_planes", {}) {
  validateName(name);
}

Structure::~Structure(){};

Structure* Structure::setEnabled(bool newEnabled) {
  if (newEnabled == isEnabled()) return this;
  enabled = newEnabled;
  return this;
};

bool Structure::isEnabled() { return enabled.get(); };

void Structure::enableIsolate() {
  for (auto& structure : polyscope::state::structures[this->typeName()]) {
    structure.second->setEnabled(false);
  }
  this->setEnabled(true);
}

void Structure::setEnabledAllOfType(bool newEnabled) {
  for (auto& structure : polyscope::state::structures[this->typeName()]) {
    structure.second->setEnabled(newEnabled);
  }
}

void Structure::buildUI() {
  ImGui::PushID(name.c_str()); // ensure there are no conflicts with
                               // identically-named labels


  if (ImGui::TreeNode(name.c_str())) {

    bool currEnabled = isEnabled();
    ImGui::Checkbox("Enabled", &currEnabled);
    setEnabled(currEnabled);
    ImGui::SameLine();

    // Options popup
    if (ImGui::Button("Options")) {
      ImGui::OpenPopup("OptionsPopup");
    }
    if (ImGui::BeginPopup("OptionsPopup")) {

      // Transform
      if (ImGui::BeginMenu("Transform")) {
        if (ImGui::MenuItem("Center")) centerBoundingBox();
        if (ImGui::MenuItem("Unit Scale")) rescaleToUnit();
        if (ImGui::MenuItem("Reset")) resetTransform();
        if (ImGui::MenuItem("Show Gizmo", NULL, &transformGizmo.enabled.get()))
          transformGizmo.enabled.manuallyChanged();
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Transparency")) {
        if (ImGui::SliderFloat("Alpha", &transparency.get(), 0., 1., "%.3f")) setTransparency(transparency.get());
        ImGui::TextUnformatted("Note: Change the transparency mode");
        ImGui::TextUnformatted("      in Appearance --> Transparency.");
        ImGui::TextUnformatted("Current mode: ");
        ImGui::SameLine();
        ImGui::TextUnformatted(modeName(render::engine->getTransparencyMode()).c_str());
        ImGui::EndMenu();
      }

      // Toggle whether slice planes apply 
      if (ImGui::BeginMenu("Slice planes")) {

        if (state::slicePlanes.empty()) {
          // if there are none, show a helpful message
          ImGui::TextUnformatted("Note: Add slice planes in");
          ImGui::TextUnformatted("      View --> Slice Planes.");
        } else {
          // otherwise, show toggles for each
          for (SlicePlane* s : state::slicePlanes) {
            bool planeEnabled = !getIgnoreSlicePlane(s->name);
            if (ImGui::MenuItem(s->name.c_str(), NULL, planeEnabled)) setIgnoreSlicePlane(s->name, planeEnabled);
          }
        }

        ImGui::EndMenu();
      }

      // Selection
      if (ImGui::BeginMenu("Structure Selection")) {
        if (ImGui::MenuItem("Enable all of type")) setEnabledAllOfType(true);
        if (ImGui::MenuItem("Disable all of type")) setEnabledAllOfType(false);
        if (ImGui::MenuItem("Isolate")) enableIsolate();
        ImGui::EndMenu();
      }

      buildStructureOptionsUI();

      // Do any structure-specific stuff here
      this->buildCustomOptionsUI();

      ImGui::EndPopup();
    }

    // Do any structure-specific stuff here
    this->buildCustomUI();

    // Build quantities list, in the common case of a QuantityStructure
    this->buildQuantitiesUI();

    ImGui::TreePop();
  }
  ImGui::PopID();
}


void Structure::buildQuantitiesUI() {}

void Structure::buildSharedStructureUI() {}

void Structure::buildStructureOptionsUI() {}

void Structure::buildCustomOptionsUI() {}

void Structure::refresh() { requestRedraw(); }

void Structure::setTransform(glm::mat4x4 transform)
{
  objectTransform = transform * objectTransform.get();
  updateStructureExtents();
}

void Structure::resetTransform() {
  objectTransform = glm::mat4(1.0);
  updateStructureExtents();
}

void Structure::centerBoundingBox() {
  std::tuple<glm::vec3, glm::vec3> bbox = boundingBox();
  glm::vec3 center = (std::get<1>(bbox) + std::get<0>(bbox)) / 2.0f;
  glm::mat4x4 newTrans = glm::translate(glm::mat4x4(1.0), -glm::vec3(center.x, center.y, center.z));
  objectTransform = newTrans * objectTransform.get();
  updateStructureExtents();
}

void Structure::rescaleToUnit() {
  double currScale = lengthScale();
  float s = static_cast<float>(1.0 / currScale);
  glm::mat4x4 newTrans = glm::scale(glm::mat4x4(1.0), glm::vec3{s, s, s});
  objectTransform = newTrans * objectTransform.get();
  updateStructureExtents();
}

glm::mat4 Structure::getModelView() { return view::getCameraViewMatrix() * objectTransform.get(); }

glm::mat4 Structure::getTransform() { return objectTransform.get(); }

void Structure::setTransformUniforms(render::ShaderProgram& p) {
  glm::mat4 viewMat = getModelView();
  p.setUniform("u_modelView", glm::value_ptr(viewMat));

  glm::mat4 projMat = view::getCameraPerspectiveMatrix();
  p.setUniform("u_projMatrix", glm::value_ptr(projMat));

  if (render::engine->transparencyEnabled()) {
    if (p.hasUniform("u_transparency")) {
      p.setUniform("u_transparency", transparency.get());
    }

    if (p.hasUniform("u_viewportDim")) {
      glm::vec4 viewport = render::engine->getCurrentViewport();
      glm::vec2 viewportDim{viewport[2], viewport[3]};
      p.setUniform("u_viewportDim", viewportDim);
    }

    // Attach the min depth texture, if needed
    // (note that this design is somewhat lazy wrt to the name of the function: it sets a texture, not a uniform, and
    // only actually does anything once on initialization)
    if (render::engine->transparencyEnabled() && p.hasTexture("t_minDepth") && !p.textureIsSet("t_minDepth")) {
      p.setTextureFromBuffer("t_minDepth", render::engine->sceneDepthMin.get());
    }
  }

  // Respect any slice planes
  for (SlicePlane* s : state::slicePlanes) {
    bool ignoreThisPlane = getIgnoreSlicePlane(s->name);
    s->setSceneObjectUniforms(p, ignoreThisPlane);
  }

  // TODO this chain if "if"s is not great. Set up some system in the render engine to conditionally set these? Maybe
  // a list of lambdas? Ugh.
  if (p.hasUniform("u_viewport_worldPos")) {
    glm::vec4 viewport = render::engine->getCurrentViewport();
    p.setUniform("u_viewport_worldPos", viewport);
  }
  if (p.hasUniform("u_invProjMatrix_worldPos")) {
    glm::mat4 P = view::getCameraPerspectiveMatrix();
    glm::mat4 Pinv = glm::inverse(P);
    p.setUniform("u_invProjMatrix_worldPos", glm::value_ptr(Pinv));
  }
  if (p.hasUniform("u_invViewMatrix_worldPos")) {
    glm::mat4 V = view::getCameraViewMatrix();
    glm::mat4 Vinv = glm::inverse(V);
    p.setUniform("u_invViewMatrix_worldPos", glm::value_ptr(Vinv));
  }
}

std::string Structure::uniquePrefix() { return typeName() + "#" + name + "#"; }

void Structure::remove() { removeStructure(typeName(), name); }


Structure* Structure::setTransparency(double newVal) {
  transparency = newVal;

  if (newVal < 1. && options::transparencyMode == TransparencyMode::None) {
    options::transparencyMode = TransparencyMode::Pretty;
  }
  requestRedraw();

  return this;
}
double Structure::getTransparency() { return transparency.get(); }

void Structure::setIgnoreSlicePlane(std::string name, bool newValue) {
  if (getIgnoreSlicePlane(name) == newValue) return;
  ignoredSlicePlaneNames.get().push_back(name);
  ignoredSlicePlaneNames.manuallyChanged();
  requestRedraw();
}

bool Structure::getIgnoreSlicePlane(std::string name) {
  std::vector<std::string>& names = ignoredSlicePlaneNames.get();
  bool ignoreThisPlane = (std::find(names.begin(), names.end(), name) != names.end());
  return ignoreThisPlane;
}

} // namespace polyscope
