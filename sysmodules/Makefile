ifeq ($(BUILD_FOR_EXPLOIT_DEV),1)
	# Keep Loader so that we can execute 3DSX
	SUBFOLDERS := loader
else
	SUBFOLDERS := loader sm pm pxi rosalina
endif

CXIS := $(foreach dir, $(SUBFOLDERS), $(dir)/$(dir).cxi)

.PHONY: all clean $(SUBFOLDERS)

all: sysmodules.bin

clean:
	@$(foreach dir, $(SUBFOLDERS), $(MAKE) -C $(dir) clean &&) true
	@rm -rf sysmodules.bin

sysmodules.bin:	$(SUBFOLDERS)
	@cat $(CXIS) > $@
	@echo built... $(notdir $@)

$(SUBFOLDERS):
	@$(MAKE) -C $@ all \
		NEXUS_VERSION_MAJOR="$(NEXUS_VERSION_MAJOR)" \
		NEXUS_VERSION_MINOR="$(NEXUS_VERSION_MINOR)" \
		NEXUS_VERSION_BUILD="$(NEXUS_VERSION_BUILD)" \
		LUMA_VERSION_MAJOR="$(LUMA_VERSION_MAJOR)" \
		LUMA_VERSION_MINOR="$(LUMA_VERSION_MINOR)" \
		LUMA_VERSION_BUILD="$(LUMA_VERSION_BUILD)"
