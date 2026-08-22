DOCKER         ?= docker
DOCKER_OPTS    ?=
DOCKER_REPO    ?= batoceralinux
DOCKER_IMAGE_NAME   ?= batocera.linux-build

ifdef IMAGE_NAME
$(warning IMAGE_NAME will be removed in the future, please migrate to DOCKER_IMAGE_NAME)
DOCKER_IMAGE_NAME := $(IMAGE_NAME)
endif

DOCKER_IMAGE = $(DOCKER_REPO)/$(DOCKER_IMAGE_NAME)

ifndef BATCH_MODE
DOCKER_OPTS += -i
endif

ifdef DIRECT_BUILD
define RUN_DOCKER
	@$(error This is a direct build environment, cannot run Docker)
endef
else
UID  := $(shell id -u)
GID  := $(shell id -g)

# Circuit-Sword local addition: BR_DOCKER_VOLUMES=1 switches DL/OUTPUT/CCACHE
# from host bind-mounts (macOS path, crosses the VirtioFS/gRPC-FUSE boundary
# on Docker Desktop for Mac -- the dominant bottleneck for this many-small-
# files build) to Docker named volumes, which live entirely inside the
# Docker Desktop Linux VM's own filesystem. No host bind-mount, no
# file-sharing translation layer, no fchmod/EIO/clock-skew quirks tied to
# that boundary (see WIFI-BUILD-FINDINGS.md failures #13/#15/#16) --
# should be substantially faster for this workload (many small files,
# many chmod/rename-heavy package installs).
#
# Named volumes are created automatically by `docker run` on first use.
# To inspect/reset them: `docker volume ls`, `docker volume rm <name>`.
# DL_DIR/OUTPUT_DIR/CCACHE_DIR must still be set (existing = macOS path)
# for non-Docker tooling that reads them (e.g. this repo's own scripts) --
# only the actual container mount source changes here.
ifdef BR_DOCKER_VOLUMES
DOCKER_VOL_DL      ?= batocera-dl
DOCKER_VOL_CCACHE  ?= batocera-ccache
DOCKER_VOL_OUTPUT  ?= batocera-output-$*

define RUN_DOCKER
	$(DOCKER) run -t --init --rm \
		-e HOME \
		-v $(PROJECT_DIR):/build \
		-v $(DOCKER_VOL_DL):/build/buildroot/dl \
		-v $(DOCKER_VOL_OUTPUT):/$* \
		-v $(DOCKER_VOL_CCACHE):$(HOME)/.buildroot-ccache \
		-w /$* \
		-v /etc/passwd:/etc/passwd:ro \
		-v /etc/group:/etc/group:ro \
		-u $(UID):$(GID) \
		$(DOCKER_OPTS) \
		$(DOCKER_IMAGE)
endef
else
define RUN_DOCKER
	$(DOCKER) run -t --init --rm \
		-e HOME \
		-v $(PROJECT_DIR):/build \
		-v $(DL_DIR):/build/buildroot/dl \
		-v $(OUTPUT_DIR)/$*:/$* \
		-v $(CCACHE_DIR):$(HOME)/.buildroot-ccache \
		-w /$* \
		-v /etc/passwd:/etc/passwd:ro \
		-v /etc/group:/etc/group:ro \
		-u $(UID):$(GID) \
		$(DOCKER_OPTS) \
		$(DOCKER_IMAGE)
endef
endif
endif

.PHONY: _check_docker
_check_docker:
ifdef DIRECT_BUILD
	$(error This is a direct build environment)
endif
	$(call REQUIRE,$(DOCKER))

DOCKER_IMAGE_STAMP = $(PROJECT_DIR)/.ba-docker-image-available
DOCKER_IMAGE_AVAILABLE := $(if $(DIRECT_BUILD),,$(DOCKER_IMAGE_STAMP))

$(DOCKER_IMAGE_STAMP): | _check_docker
	@$(call MESSAGE,$(DOCKER_ACTION_MESSAGE) docker image $(DOCKER_IMAGE))
	$(if $(filter build,$(DOCKER_ACTION)),\
		$(DOCKER) build -t $(DOCKER_IMAGE) .,\
		$(DOCKER) pull $(DOCKER_IMAGE))
	@touch $@

.PHONY: pull-docker-image
pull-docker-image: DOCKER_ACTION = pull
pull-docker-image: DOCKER_ACTION_MESSAGE = Pulling
pull-docker-image: $(DOCKER_IMAGE_AVAILABLE)

.PHONY: build-docker-image
build-docker-image: DOCKER_ACTION = build
build-docker-image: DOCKER_ACTION_MESSAGE = Building
build-docker-image: $(DOCKER_IMAGE_AVAILABLE)

.PHONY: clean-for-docker-image
clean-for-docker-image:
	-@rm -f $(DOCKER_IMAGE_STAMP) >/dev/null

.PHONY: update-docker-image
update-docker-image: clean-for-docker-image
	@$(MAKE) pull-docker-image DOCKER_ACTION_MESSAGE=Updating

.PHONY: rebuild-docker-image
rebuild-docker-image: clean-for-docker-image
	@$(MAKE) build-docker-image DOCKER_ACTION_MESSAGE=Rebuilding

.PHONY: publish-docker-image
publish-docker-image: | _check_docker
	@$(call MESSAGE,Publishing docker image $(DOCKER_IMAGE))
	@$(DOCKER) push $(DOCKER_IMAGE):latest
