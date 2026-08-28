#!/usr/bin/env bash

# This library exports clipboard state to the bridge that sources it.
# shellcheck disable=SC2034

# Probe richer formats first so copying an image from an application that also
# offers a textual fallback preserves the image. Each entry is:
# source target | canonical Wayland MIME | X11 target used when forwarding.
SPICE_CLIPBOARD_TARGETS=(
  'image/png|image/png|image/png'
  'image/jpeg|image/jpeg|image/jpeg'
  'image/tiff|image/tiff|image/tiff'
  'image/bmp|image/bmp|image/bmp'
  'image/x-bmp|image/bmp|image/bmp'
  'image/x-MS-bmp|image/bmp|image/bmp'
  'image/x-win-bitmap|image/bmp|image/bmp'
  'text/plain;charset=utf-8|text/plain|UTF8_STRING'
  'UTF8_STRING|text/plain|UTF8_STRING'
  'text/plain|text/plain|UTF8_STRING'
  'TEXT|text/plain|UTF8_STRING'
  'STRING|text/plain|UTF8_STRING'
)

SPICE_CLIPBOARD_MARKER_MIME=application/x-spice-guest-tools

clipboard_target_is_offered() {
  local offered_targets=$1 wanted=$2 offered
  while IFS= read -r offered; do
    [[ ${offered} == "${wanted}" ]] && return 0
  done <<<"${offered_targets}"
  return 1
}

clipboard_read_x11() {
  local destination=$1 descriptor source_target canonical_mime x11_target
  local offered_targets
  SPICE_CLIPBOARD_OWNED=false
  if ! offered_targets=$(xclip -selection clipboard -target TARGETS -out 2>/dev/null); then
    if xclip -selection clipboard -out >"${destination}" 2>/dev/null; then
      SPICE_CLIPBOARD_MIME=text/plain
      SPICE_CLIPBOARD_X11_TARGET=UTF8_STRING
      return 0
    fi
    return 1
  fi
  if clipboard_target_is_offered "${offered_targets}" "${SPICE_CLIPBOARD_MARKER_MIME}"; then
    SPICE_CLIPBOARD_OWNED=true
    return 1
  fi
  SPICE_CLIPBOARD_OWNED=false
  for descriptor in "${SPICE_CLIPBOARD_TARGETS[@]}"; do
    IFS='|' read -r source_target canonical_mime x11_target <<<"${descriptor}"
    if clipboard_target_is_offered "${offered_targets}" "${source_target}" &&
      xclip -selection clipboard -target "${source_target}" -out \
        >"${destination}" 2>/dev/null; then
      SPICE_CLIPBOARD_MIME=${canonical_mime}
      SPICE_CLIPBOARD_X11_TARGET=${x11_target}
      return 0
    fi
  done
  return 1
}

clipboard_read_wayland() {
  local destination=$1 descriptor source_target canonical_mime x11_target
  local offered_targets
  SPICE_CLIPBOARD_OWNED=false
  if ! offered_targets=$(wl-paste --list-types 2>/dev/null); then
    if wl-paste --no-newline --type text >"${destination}" 2>/dev/null; then
      SPICE_CLIPBOARD_MIME=text/plain
      SPICE_CLIPBOARD_X11_TARGET=UTF8_STRING
      return 0
    fi
    return 1
  fi
  if clipboard_target_is_offered "${offered_targets}" "${SPICE_CLIPBOARD_MARKER_MIME}"; then
    SPICE_CLIPBOARD_OWNED=true
    return 1
  fi
  SPICE_CLIPBOARD_OWNED=false
  for descriptor in "${SPICE_CLIPBOARD_TARGETS[@]}"; do
    IFS='|' read -r source_target canonical_mime x11_target <<<"${descriptor}"
    if clipboard_target_is_offered "${offered_targets}" "${source_target}" &&
      wl-paste --no-newline --type "${source_target}" \
        >"${destination}" 2>/dev/null; then
      SPICE_CLIPBOARD_MIME=${canonical_mime}
      SPICE_CLIPBOARD_X11_TARGET=${x11_target}
      return 0
    fi
  done
  return 1
}

clipboard_write_wayland() {
  local source=$1 mime=$2
  local max_bytes=${CLIPBOARD_MAX_BYTES:-104857600}
  local max_pixels=${CLIPBOARD_MAX_PIXELS:-67108864}
  local derived_formats=${CLIPBOARD_DERIVED_FORMATS:-image/png}
  local -a arguments=(
    --wayland-only
    --mime "${mime}"
    --max-bytes "${max_bytes}"
    --max-pixels "${max_pixels}"
  )
  if [[ ,${derived_formats}, == *,image/png,* ]]; then
    arguments+=(--derive-png)
  fi
  spice-clipboard-provider "${arguments[@]}" "${source}"
}

clipboard_write_x11() {
  local source=$1 target=$2
  xclip -selection clipboard -target "${target}" -in <"${source}"
}

clipboard_item_is_duplicate() {
  local current=$1 mime=$2 previous=$3 previous_mime_file=$4 previous_mime=""
  [[ -f ${previous} && -f ${previous_mime_file} ]] || return 1
  IFS= read -r previous_mime <"${previous_mime_file}" || true
  [[ ${mime} == "${previous_mime}" ]] && cmp -s "${current}" "${previous}"
}

clipboard_remember_item() {
  local current=$1 mime=$2 previous=$3 previous_mime_file=$4
  cp -- "${current}" "${previous}"
  printf '%s\n' "${mime}" >"${previous_mime_file}"
}
