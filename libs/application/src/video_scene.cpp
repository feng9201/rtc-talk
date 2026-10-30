#include "rtcwin/application/video_scene.h"

#include <algorithm>

namespace rtcwin::application {
namespace {

struct MemberView {
  std::string uid;
  std::string display_name;
  domain::RoomRole role = domain::RoomRole::Member;
  bool self = false;
  bool selected = false;
  bool camera_on = false;
  bool screen_on = false;
};

std::vector<MemberView> BuildMembers(const domain::SessionSnapshot& snapshot) {
  std::vector<MemberView> members;
  if (!snapshot.self_uid.empty() || !snapshot.self_display_name.empty()) {
    members.push_back(MemberView{
        snapshot.self_uid,
        snapshot.self_display_name,
        snapshot.self_role,
        true,
        snapshot.selected_uid == snapshot.self_uid,
        snapshot.camera_on,
        snapshot.sharing});
  }
  for (const auto& peer : snapshot.peers) {
    members.push_back(MemberView{
        peer.uid,
        peer.display_name,
        peer.role,
        false,
        snapshot.selected_uid == peer.uid,
        peer.camera_on,
        peer.screen_on});
  }
  return members;
}

bool HasVideo(const MemberView& member) {
  return member.camera_on || member.screen_on;
}

VideoSceneItem MakeItem(const MemberView& member,
                        domain::VideoSource source,
                        bool has_camera_pip = false) {
  VideoSceneItem item;
  item.uid = member.uid;
  item.display_name = member.display_name;
  item.source = source;
  item.self = member.self;
  item.selected = member.selected;
  item.has_camera_pip = has_camera_pip;
  return item;
}

VideoSceneItem PreferredItem(const MemberView& member) {
  if (member.screen_on) {
    return MakeItem(member, domain::VideoSource::Screen, member.camera_on);
  }
  return MakeItem(member, domain::VideoSource::Camera);
}

const MemberView* FindFocus(const std::vector<MemberView>& members,
                            const std::string& selected_uid) {
  const auto selected = std::find_if(
      members.begin(), members.end(),
      [&selected_uid](const MemberView& member) {
        return !selected_uid.empty() && member.uid == selected_uid;
      });
  if (selected != members.end()) return &*selected;
  return members.empty() ? nullptr : &members.front();
}

void AppendGridItems(const std::vector<MemberView>& members,
                     domain::StageFilter filter,
                     std::vector<VideoSceneItem>* output) {
  for (const auto& member : members) {
    // Grid mode is a stream view, not a member view. A member with two active
    // producers must therefore contribute two independently addressable tiles.
    if (member.screen_on) {
      output->push_back(MakeItem(member, domain::VideoSource::Screen));
    }
    if (member.camera_on) {
      output->push_back(MakeItem(member, domain::VideoSource::Camera));
    }
    if (!member.screen_on && !member.camera_on &&
        filter == domain::StageFilter::All) {
      output->push_back(MakeItem(member, domain::VideoSource::Camera));
    }
  }
}

}  // namespace

VideoScene VideoSceneBuilder::Build(
    const domain::SessionSnapshot& snapshot, domain::StageLayout layout,
    domain::StageFilter filter) {
  const auto members = BuildMembers(snapshot);
  VideoScene scene;

  const auto sharing = std::find_if(
      members.begin(), members.end(),
      [&snapshot](const MemberView& member) {
        return member.screen_on && member.selected &&
               snapshot.selected_uid == member.uid;
      });
  const auto any_sharing = std::find_if(
      members.begin(), members.end(),
      [](const MemberView& member) { return member.screen_on; });
  const auto shared = sharing != members.end() ? sharing : any_sharing;

  if (shared != members.end()) {
    scene.mode = VideoSceneMode::Shared;
    scene.primary.push_back(
        MakeItem(*shared, domain::VideoSource::Screen, shared->camera_on));
    for (const auto& member : members) {
      if (member.uid == shared->uid) continue;
      if (filter == domain::StageFilter::VideoOnly && !HasVideo(member)) {
        continue;
      }
      scene.secondary.push_back(PreferredItem(member));
    }
    return scene;
  }

  if (layout == domain::StageLayout::Focus && members.size() > 1) {
    scene.mode = VideoSceneMode::Focus;
    const MemberView* focus = FindFocus(members, snapshot.selected_uid);
    if (focus != nullptr) {
      scene.primary.push_back(PreferredItem(*focus));
    }
    for (const auto& member : members) {
      if (focus != nullptr && member.uid == focus->uid) continue;
      if (filter == domain::StageFilter::VideoOnly && !HasVideo(member)) {
        continue;
      }
      scene.secondary.push_back(PreferredItem(member));
    }
    return scene;
  }

  scene.mode = VideoSceneMode::Grid;
  AppendGridItems(members, filter, &scene.primary);
  return scene;
}

std::string VideoSceneKey(const VideoSceneItem& item) {
  return item.uid + "|" +
         (item.source == domain::VideoSource::Screen ? "screen" : "camera");
}

}  // namespace rtcwin::application
