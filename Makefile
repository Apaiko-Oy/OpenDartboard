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

deb:
ifndef VERSION
	$(error VERSION is not set, usage: make deb VERSION=X.X.X)
endif
	rm -rf dist/staging
	mkdir -p dist/staging/opendartboard_$(VERSION)/DEBIAN
	mkdir -p dist/staging/opendartboard_$(VERSION)/usr/local/bin

	# Use envsubst to inject version/name into the control file
	env PKG_NAME=opendartboard PKG_VERSION=$(VERSION) \
		envsubst < distributions/debian_arm64/control \
		> dist/staging/opendartboard_$(VERSION)/DEBIAN/control

	# Copy binary into correct path
	cp /usr/local/bin/opendartboard dist/staging/opendartboard_$(VERSION)/usr/local/bin/

	# Build the .deb
	dpkg-deb --build dist/staging/opendartboard_$(VERSION) dist/

	@echo "\033[32mBuilt .deb package: dist/opendartboard_$(VERSION).deb\033[0m"


release:
    # just run the ./scripts/release.sh script
	@echo "\033[32mRunning release script...\033[0m"
	@./scripts/release.sh