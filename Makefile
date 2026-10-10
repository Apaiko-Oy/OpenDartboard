.PHONY: build build-dev run deb release

PROJECT_VERSION_VAL := $(if $(VERSION),$(VERSION),0.0.0-dev)
# Debug-only defines. These are NOT part of a release build: DEBUG_VIA_VIDEO_INPUT
# puts a 16.7 ms sleep in every capture cycle and opens an MJPEG listener on 8081,
# and DEBUG_SEEK_VIDEO seeks a file source past its first three seconds -- which moves
# the thirty-frame calibration window, so a dev and a release binary of one commit
# calibrate on different pictures and can ADMIT different cameras on one fixture
# (#1551: rig-20260922 camera 1 reads R=0.578 at the dev window, R=0.873 at the
# opening, against the 0.60 gate). A census belongs to the build that measured it;
# OD_SEEK_VIDEO=off holds a dev binary at the opening on the same build.
DEV_DEFS = -DDEBUG_SEEK_VIDEO -DDEBUG_VIA_VIDEO_INPUT
OD_DEFS ?=

# #1408: the update trust anchors (ADR-0077 §4), taken from the environment and never
# typed into a source file. Unset is legal and is what an ordinary build has: the binary
# then answers NoAnchor to every manifest, which is what every build before #1408 did.
# .github/workflows/release.yml sets them from an Actions variable, so rotating a key is a
# variable and not a commit, and CMakeLists.txt refuses a malformed one outright.
OD_ANCHOR_FLAGS = $(if $(OD_UPDATE_ANCHOR_CURRENT),-DOD_UPDATE_ANCHOR_CURRENT=$(OD_UPDATE_ANCHOR_CURRENT)) \
                  $(if $(OD_UPDATE_ANCHOR_NEXT),-DOD_UPDATE_ANCHOR_NEXT=$(OD_UPDATE_ANCHOR_NEXT))

CMAKE_FLAGS = -DCMAKE_PREFIX_PATH=/usr/local \
              -DCMAKE_CXX_FLAGS="$(OD_DEFS)" \
              -DAPP_VERSION=$(PROJECT_VERSION_VAL) \
              $(OD_ANCHOR_FLAGS)

build-dev: OD_DEFS = $(DEV_DEFS)
build-dev: build

build:
	mkdir -p build
	cmake -S . -B build $(CMAKE_FLAGS)
	cmake --build build -- -j4 --no-print-directory
	@cp build/opendartboard /usr/local/bin/opendartboard
	@echo "\033[32mBuild completed successfully!\033[0m"

run-mocks:
	opendartboard --debug \
		--cams mocks/cam_1.mp4,mocks/cam_2.mp4,mocks/cam_3.mp4 \
		--width 1280 --height 720

run:
	opendartboard --debug --autocams --width 1280 --height 720 --fps 30

# #1659: what the package carries. Until #1659 this target staged DEBIAN/control and the
# binary and nothing else, so a Pi installed from the release had no unit, no lock_cams.sh
# and nothing that started the detector at boot -- while postinst, postrm and both unit
# templates sat in the tree beside it. testers/i1659_deb_check.sh holds the list.
#
# The camera mode both units are rendered with. The detector's --width/--height/--fps and
# lock_cams.sh's v4l2 mode must be the same numbers, so they are one set of variables and
# lock_cams.service passes them to the script. 1280x720@30 is what `make run` and every
# unit tester (i1334, i1383, i1660) render the template with.
DEB_WIDTH ?= 1280
DEB_HEIGHT ?= 720
DEB_FPS ?= 30
# envsubst is told which variables are its to replace. Unrestricted, it would also replace
# $HOME, $XDG_CONFIG_HOME and $STATE_DIRECTORY in the unit's own comments with whatever the
# build shell happened to hold -- nothing, in CI.
DEB_UNIT_VARS = '$${WIDTH} $${HEIGHT} $${FPS}'
DEB_ROOT = dist/staging/opendartboard_$(VERSION)

deb:
ifndef VERSION
	$(error VERSION is not set, usage: make deb VERSION=X.X.X)
endif
	rm -rf dist/staging
	mkdir -p $(DEB_ROOT)/DEBIAN

	# Use envsubst to inject version/name into the control file
	env PKG_NAME=opendartboard PKG_VERSION=$(VERSION) \
		envsubst < distributions/debian_arm64/control \
		> $(DEB_ROOT)/DEBIAN/control

	# Maintainer scripts: enable and start on configure, stop and disable on remove.
	install -m 0755 distributions/debian_arm64/postinst $(DEB_ROOT)/DEBIAN/postinst
	install -m 0755 distributions/debian_arm64/prerm $(DEB_ROOT)/DEBIAN/prerm
	install -m 0755 distributions/debian_arm64/postrm $(DEB_ROOT)/DEBIAN/postrm

	# The detector, and the script lock_cams.service runs
	install -D -m 0755 /usr/local/bin/opendartboard $(DEB_ROOT)/usr/local/bin/opendartboard
	install -D -m 0755 scripts/lock_cams.sh $(DEB_ROOT)/usr/local/bin/lock_cams.sh

	# The model the unit's ExecStart names with --model
	install -d -m 0755 $(DEB_ROOT)/usr/local/share/opendartboard/models
	install -m 0644 models/dart.param models/dart.bin $(DEB_ROOT)/usr/local/share/opendartboard/models/

	# Both units, rendered from their templates
	install -d -m 0755 $(DEB_ROOT)/lib/systemd/system
	for unit in opendartboard lock_cams; do \
		env WIDTH=$(DEB_WIDTH) HEIGHT=$(DEB_HEIGHT) FPS=$(DEB_FPS) \
			envsubst $(DEB_UNIT_VARS) < templates/$$unit.service.template \
			> $(DEB_ROOT)/lib/systemd/system/$$unit.service || exit 1; \
		chmod 0644 $(DEB_ROOT)/lib/systemd/system/$$unit.service; \
	done

	# #1801: the age at which the unit's per-start log files are pruned
	install -D -m 0644 distributions/debian_arm64/tmpfiles.conf $(DEB_ROOT)/usr/lib/tmpfiles.d/opendartboard.conf

	# Build the .deb. --root-owner-group: the files belong to root on the board, not to
	# whoever ran the build.
	dpkg-deb --root-owner-group --build $(DEB_ROOT) dist/

	@echo "\033[32mBuilt .deb package: dist/opendartboard_$(VERSION).deb\033[0m"

release:
    # just run the ./scripts/release.sh script
	@echo "\033[32mRunning release script...\033[0m"
	@./scripts/release.sh