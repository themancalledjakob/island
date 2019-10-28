#ifndef GUARD_LE_BACKEND_VK_H
#define GUARD_LE_BACKEND_VK_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void register_le_backend_vk_api( void *api );
void register_le_instance_vk_api( void *api );       // for le_instance_vk.cpp
void register_le_allocator_linear_api( void *api_ ); // for le_allocator.cpp
void register_le_device_vk_api( void *api );         // for le_device_vk.cpp
void register_le_pipeline_vk_api( void *api );       // for le_pipeline_vk.cpp

struct le_backend_o;
struct le_backend_vk_api;

struct le_swapchain_settings_t;
struct pal_window_o;

struct le_backend_vk_instance_o; // defined in le_instance_vk.cpp
struct le_device_o;              // defined in le_device_vk.cpp
struct le_renderpass_o;
struct le_buffer_o;
struct le_allocator_o;
struct le_staging_allocator_o;
struct le_resource_handle_t; // defined in renderer_types

struct le_pipeline_manager_o;
struct le_shader_module_o;
struct graphics_pipeline_state_o; // for le_pipeline_builder
struct compute_pipeline_state_o;  // for le_pipeline_builder
struct le_gpso_handle_t;          // opaque handle representing graphics pipeline state object hash
struct le_cpso_handle_t;          // opaque handle representing compute  pipeline state object hash

struct VkInstance_T;
struct VkDevice_T;
struct VkImage_T;
struct VkBuffer_T;
struct VkPhysicalDevice_T;
struct VkQueue_T;
struct VkPhysicalDeviceProperties;
struct VkPhysicalDeviceMemoryProperties;
struct VkImageCreateInfo;
struct VkBufferCreateInfo;
struct VkMemoryRequirements;
struct VkMemoryAllocateInfo;

struct LeRenderPass;

struct VmaAllocator_T;
struct VmaAllocation_T;
struct VmaAllocationCreateInfo;
struct VmaAllocationInfo;

struct LeShaderStageEnum;
enum class LeResourceType : uint8_t;

struct VkFormatEnum; // wrapper around `vk::Format`. Defined in <le_backend_types_internal.h>

struct le_resource_info_t;

struct le_backend_vk_settings_t {
	const char **            requestedExtensions    = nullptr;
	uint32_t                 numRequestedExtensions = 0;
	uint32_t                 concurrency_count      = 1;       // number of potential worker threads
	pal_window_o *           pWindow                = nullptr; // non-owning, owned by application. Application must outlive backend.
	le_swapchain_settings_t *pSwapchain_settings    = nullptr; // non-owning, owned by caller of setup method.
};

struct le_pipeline_layout_info {
	uint64_t pipeline_layout_key  = 0;  // handle to pipeline layout
	uint64_t set_layout_keys[ 8 ] = {}; // maximum number of DescriptorSets is 8
	uint64_t set_layout_count     = 0;  // number of actually used DescriptorSetLayouts for this layout
};

struct le_pipeline_and_layout_info_t {
	struct VkPipeline_T *   pipeline;
	le_pipeline_layout_info layout_info;
};

struct le_backend_vk_api {
	static constexpr auto id      = "le_backend_vk";
	static constexpr auto pRegFun = register_le_backend_vk_api;

	// clang-format off
	struct backend_vk_interface_t {
		le_backend_o *         ( *create                     ) ( );
		void                   ( *destroy                    ) ( le_backend_o *self );

		void                   ( *setup                      ) ( le_backend_o *self, le_backend_vk_settings_t *settings );

		bool                   ( *poll_frame_fence           ) ( le_backend_o* self, size_t frameIndex);
		bool                   ( *clear_frame                ) ( le_backend_o *self, size_t frameIndex );
		void                   ( *process_frame              ) ( le_backend_o *self, size_t frameIndex );
		bool                   ( *acquire_physical_resources ) ( le_backend_o *self, size_t frameIndex, le_renderpass_o **passes, size_t numRenderPasses, le_resource_handle_t const * declared_resources, le_resource_info_t const * declared_resources_infos, size_t const & declared_resources_count );
		bool                   ( *dispatch_frame             ) ( le_backend_o *self, size_t frameIndex );

		size_t                 ( *get_num_swapchain_images   ) ( le_backend_o *self );
		void                   ( *reset_swapchain            ) ( le_backend_o *self );
		le_allocator_o**       ( *get_transient_allocators   ) ( le_backend_o* self, size_t frameIndex);
		le_staging_allocator_o*( *get_staging_allocator      ) ( le_backend_o* self, size_t frameIndex);

		le_shader_module_o*    ( *create_shader_module       ) ( le_backend_o* self, char const * path, const LeShaderStageEnum& moduleType, char const * macro_definitions);
		void                   ( *update_shader_modules      ) ( le_backend_o* self );

		le_pipeline_manager_o* ( *get_pipeline_cache         ) ( le_backend_o* self);

		void                   ( *get_swapchain_extent  ) ( le_backend_o* self, uint32_t * p_width, uint32_t * p_height);
		le_resource_handle_t   ( *get_swapchain_resource    ) ( le_backend_o* self);
	};

	struct private_backend_vk_interface_t {
		le_device_o*        (*get_le_device            )(le_backend_o* self);
		VkDevice_T*         (*get_vk_device            )(le_backend_o* self);
		VkPhysicalDevice_T* (*get_vk_physical_device   )(le_backend_o* self);

		int32_t ( *allocate_image )
		(
		    le_backend_o*                   self,
		    VkImageCreateInfo const *       pImageCreateInfo,
		    VmaAllocationCreateInfo const * pAllocationCreateInfo,
		    VkImage_T **                    pImage,
		    VmaAllocation_T **              pAllocation,
		    VmaAllocationInfo *             pAllocationInfo
		);

		void (*destroy_image)(le_backend_o* self, struct VkImage_T * image, struct VmaAllocation_T* allocation);

		int32_t ( *allocate_buffer )
		(
		    le_backend_o*                  self,
		    VkBufferCreateInfo const*      pBufferCreateInfo,
		    VmaAllocationCreateInfo const *pAllocationCreateInfo,
		    VkBuffer_T* *                  pBuffer,
		    VmaAllocation_T **             pAllocation,
		    VmaAllocationInfo *            pAllocationInfo
		);

		void ( *destroy_buffer )(le_backend_o* self, struct VkBuffer_T * buffer, struct VmaAllocation_T* allocation);

	};

	struct instance_interface_t {
		le_backend_vk_instance_o *  ( *create           ) ( const char** requestedExtensionNames_, uint32_t requestedExtensionNamesCount_ );
		void                        ( *destroy          ) ( le_backend_vk_instance_o* self_ );
		void                        ( *post_reload_hook ) ( le_backend_vk_instance_o* self_ );
		VkInstance_T*               ( *get_vk_instance  ) ( le_backend_vk_instance_o* self_ );
		bool                        ( *is_extension_available ) ( le_backend_vk_instance_o* self, char const * extension_name);

	};

	struct device_interface_t {
		le_device_o *    ( *create                                  ) ( le_backend_vk_instance_o* instance_ );
		void                        ( *destroy                                 ) ( le_device_o* self_ );

		void                        ( *decrease_reference_count                ) ( le_device_o* self_ );
		void                        ( *increase_reference_count                ) ( le_device_o* self_ );
		uint32_t                    ( *get_reference_count                     ) ( le_device_o* self_ );

		uint32_t                    ( *get_default_graphics_queue_family_index ) ( le_device_o* self_ );
		uint32_t                    ( *get_default_compute_queue_family_index  ) ( le_device_o* self_ );
		VkQueue_T *                 ( *get_default_graphics_queue              ) ( le_device_o* self_ );
		VkQueue_T *                 ( *get_default_compute_queue               ) ( le_device_o* self_ );
		VkFormatEnum                ( *get_default_depth_stencil_format        ) ( le_device_o* self_ );
		VkPhysicalDevice_T*         ( *get_vk_physical_device                  ) ( le_device_o* self_ );
		VkDevice_T*                 ( *get_vk_device                           ) ( le_device_o* self_ );
		bool                        ( *is_extension_available                  ) ( le_device_o* self, char const * extension_name);

		const VkPhysicalDeviceProperties&       ( *get_vk_physical_device_properties        ) ( le_device_o* self );
		const VkPhysicalDeviceMemoryProperties& ( *get_vk_physical_device_memory_properties ) ( le_device_o* self );
		bool                                    ( *get_memory_allocation_info               ) ( le_device_o *self, const VkMemoryRequirements &memReqs, const uint32_t &memPropsRef, VkMemoryAllocateInfo *pMemoryAllocationInfo );
	};

	struct le_pipeline_manager_interface_t {
		le_pipeline_manager_o*                   ( *create                            ) ( VkDevice_T* device          );
		void                                     ( *destroy                           ) ( le_pipeline_manager_o* self );

		void                                     ( *introduce_graphics_pipeline_state ) ( le_pipeline_manager_o *self, graphics_pipeline_state_o* gpso, le_gpso_handle_t* gpsoHandle);
		void                                     ( *introduce_compute_pipeline_state  ) ( le_pipeline_manager_o *self, compute_pipeline_state_o* cpso, le_cpso_handle_t* cpsoHandle);

		le_pipeline_and_layout_info_t            ( *produce_pipeline                  ) ( le_pipeline_manager_o *self, le_gpso_handle_t* gpsoHandle, const LeRenderPass &pass, uint32_t subpass ) ;
		le_pipeline_and_layout_info_t            ( *produce_compute_pipeline          ) ( le_pipeline_manager_o *self, le_cpso_handle_t* gpsoHandle);

		le_shader_module_o*                      ( *create_shader_module              ) ( le_pipeline_manager_o* self, char const * path, const LeShaderStageEnum& moduleType, char const *macro_definitions);
		void                                     ( *update_shader_modules             ) ( le_pipeline_manager_o* self );
		struct VkPipelineLayout_T*               ( *get_pipeline_layout               ) ( le_pipeline_manager_o* self, uint64_t pipeline_layout_key);
		const struct le_descriptor_set_layout_t& ( *get_descriptor_set_layout         ) ( le_pipeline_manager_o* self, uint64_t setlayout_key);
	};

	struct allocator_linear_interface_t {
		le_allocator_o *        ( *create               ) ( VmaAllocationInfo const *info, uint16_t alignment);
		void                    ( *destroy              ) ( le_allocator_o* self );
		bool                    ( *allocate             ) ( le_allocator_o* self, uint64_t numBytes, void ** pData, uint64_t* bufferOffset);
		void                    ( *reset                ) ( le_allocator_o* self );
		le_resource_handle_t    ( *get_le_resource_id   ) ( le_allocator_o* self );
	};

	struct staging_allocator_interface_t {
		le_staging_allocator_o* ( *create  )( VmaAllocator_T* const vmaAlloc, VkDevice_T* const device );
		void                    ( *destroy )( le_staging_allocator_o* self ) ;
		void                    ( *reset   )( le_staging_allocator_o* self );
		bool                    ( *map     )( le_staging_allocator_o* self, uint64_t numBytes, void **pData, le_resource_handle_t *resource_handle );
	};

	struct shader_module_interface_t {
		uint64_t          ( * get_hash                   ) ( le_shader_module_o* module );
		LeShaderStageEnum ( * get_stage                  ) ( le_shader_module_o* module );
	};

	// clang-format on

	allocator_linear_interface_t    le_allocator_linear_i;
	instance_interface_t            vk_instance_i;
	device_interface_t              vk_device_i;
	backend_vk_interface_t          vk_backend_i;
	le_pipeline_manager_interface_t le_pipeline_manager_i;
	shader_module_interface_t       le_shader_module_i;
	staging_allocator_interface_t   le_staging_allocator_i;

	private_backend_vk_interface_t private_backend_vk_i;

	mutable le_backend_vk_instance_o *cUniqueInstance = nullptr;
};

#ifdef __cplusplus
} // extern "C"

namespace le_backend_vk {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_backend_vk_api>( true );
#	else
const auto api = Registry::addApiStatic<le_backend_vk_api>();
#	endif

static const auto &vk_backend_i           = api -> vk_backend_i;
static const auto &private_backend_vk_i   = api -> private_backend_vk_i;
static const auto &le_allocator_linear_i  = api -> le_allocator_linear_i;
static const auto &le_staging_allocator_i = api -> le_staging_allocator_i;
static const auto &vk_instance_i          = api -> vk_instance_i;
static const auto &vk_device_i            = api -> vk_device_i;
static const auto &le_pipeline_manager_i  = api -> le_pipeline_manager_i;
static const auto &le_shader_module_i     = api -> le_shader_module_i;

} // namespace le_backend_vk

namespace le {

class Backend : NoCopy, NoMove {
	le_backend_o *self         = nullptr;
	bool          is_reference = false;

  public:
	operator auto() {
		return self;
	}

	Backend()
	    : self( le_backend_vk::vk_backend_i.create() )
	    , is_reference( false ) {
	}

	Backend( le_backend_o *ref )
	    : self( ref )
	    , is_reference( true ) {
	}

	~Backend() {
		if ( !is_reference ) {
			le_backend_vk::vk_backend_i.destroy( self );
		}
	}

	void setup( le_backend_vk_settings_t *settings ) {
		le_backend_vk::vk_backend_i.setup( self, settings );
	}

	bool clearFrame( size_t frameIndex ) {
		return le_backend_vk::vk_backend_i.clear_frame( self, frameIndex );
	}

	void processFrame( size_t frameIndex ) {
		le_backend_vk::vk_backend_i.process_frame( self, frameIndex );
	}

	size_t getNumSwapchainImages() {
		return le_backend_vk::vk_backend_i.get_num_swapchain_images( self );
	}

	bool dispatchFrame( size_t frameIndex ) {
		return le_backend_vk::vk_backend_i.dispatch_frame( self, frameIndex );
	}
};

class Instance {
	le_backend_vk_instance_o *self = nullptr;

  public:
	Instance( const char **extensionsArray_ = nullptr, uint32_t numExtensions_ = 0 )
	    : self( le_backend_vk::vk_instance_i.create( extensionsArray_, numExtensions_ ) ) {
	}

	~Instance() {
		le_backend_vk::vk_instance_i.destroy( self );
	}

	VkInstance_T *getVkInstance() {
		return le_backend_vk::vk_instance_i.get_vk_instance( self );
	}

	bool isExtensionAvailable( char const *extensionName ) {
		return le_backend_vk::vk_instance_i.is_extension_available( self, extensionName );
	}

	operator auto() {
		return self;
	}
};

class Device : NoCopy, NoMove {
	le_device_o *self = nullptr;

  public:
	Device( le_backend_vk_instance_o *instance_ )
	    : self( le_backend_vk::vk_device_i.create( instance_ ) ) {
		le_backend_vk::vk_device_i.increase_reference_count( self );
	}

	~Device() {
		le_backend_vk::vk_device_i.decrease_reference_count( self );
		if ( le_backend_vk::vk_device_i.get_reference_count( self ) == 0 ) {
			le_backend_vk::vk_device_i.destroy( self );
		}
	}

	// copy constructor
	Device( const Device &lhs )
	    : self( lhs.self ) {
		le_backend_vk::vk_device_i.increase_reference_count( self );
	}

	// reference from data constructor
	Device( le_device_o *device_ )
	    : self( device_ ) {
		le_backend_vk::vk_device_i.increase_reference_count( self );
	}

	VkDevice_T *getVkDevice() const {
		return le_backend_vk::vk_device_i.get_vk_device( self );
	}

	VkPhysicalDevice_T *getVkPhysicalDevice() const {
		return le_backend_vk::vk_device_i.get_vk_physical_device( self );
	}

	uint32_t getDefaultGraphicsQueueFamilyIndex() const {
		return le_backend_vk::vk_device_i.get_default_graphics_queue_family_index( self );
	}

	uint32_t getDefaultComputeQueueFamilyIndex() const {
		return le_backend_vk::vk_device_i.get_default_compute_queue_family_index( self );
	}

	VkQueue_T *getDefaultGraphicsQueue() const {
		return le_backend_vk::vk_device_i.get_default_graphics_queue( self );
	}

	VkQueue_T *getDefaultComputeQueue() const {
		return le_backend_vk::vk_device_i.get_default_compute_queue( self );
	}

	bool isExtensionAvailable( char const *extensionName ) const {
		return le_backend_vk::vk_device_i.is_extension_available( self, extensionName );
	}

	operator auto() {
		return self;
	}
};

} // namespace le
#endif // __cplusplus
#endif // GUARD_PAL_BACKEND_VK_H
