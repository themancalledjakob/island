#include "hello_world_app.h"

#include "pal_window/pal_window.h"
#include "le_ui_event/le_ui_event.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"

#include "le_camera/le_camera.h"
#include "le_pipeline_builder/le_pipeline_builder.h"

#include "le_mesh_generator/le_mesh_generator.h"
#include "le_pixels/le_pixels.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#define GLM_ENABLE_EXPERIMENTAL
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "glm/gtx/string_cast.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include <chrono>

using NanoTime = std::chrono::time_point<std::chrono::high_resolution_clock>;

struct Image : NoCopy, NoMove {
	le_resource_handle_t imageHandle{};
	le_resource_info_t   imageInfo{};
	le_resource_handle_t textureHandle{};
	le_pixels_o *        pixels; // owned
	le_pixels_info       pixelsInfo;
	bool                 wasLoaded{};

	~Image() {

		if ( pixels ) {
			using namespace le_pixels;
			le_pixels_i.destroy( pixels );
		}
	}
};

struct WorldGeometry {
	le_resource_handle_t    vertex_buffer_handle = LE_BUF_RESOURCE( "WORLD_VERTICES" );
	le_resource_info_t      vertex_buffer_info{};
	std::array<uint64_t, 4> buffer_offsets{};
	size_t                  vertexDataByteCount{};   // total byte count of vertex data
	size_t                  vertexCount         = 0; // number of Vertices
	le_resource_handle_t    index_buffer_handle = LE_BUF_RESOURCE( "WORLD_INDICES" );
	le_resource_info_t      index_buffer_info{};
	size_t                  indexDataByteCount;
	size_t                  indexCount; // number of indices
	bool                    wasLoaded;
};

struct hello_world_app_o {
	le::Backend  backend;
	pal::Window  window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	LeCameraController cameraController;
	LeCamera           camera;
	LeMeshGenerator    sphereGenerator;

	Image         imgEarthAlbedo{};
	Image         imgEarthNight{};
	Image         imgEarthClouds{};
	Image         imgEarthNormals{};
	WorldGeometry worldGeometry{};
	NanoTime      timeStamp{};
	double        timeDelta{};       // time since last frame, in s
	double        earthRotation = 0; // day/night cycle
	bool          animate       = true;
};

static void hello_world_app_process_ui_events( hello_world_app_o *self ); //ffdecl

// ----------------------------------------------------------------------

static void initialize() {
	pal::Window::init();
};

// ----------------------------------------------------------------------

static void terminate() {
	pal::Window::terminate();
};

static void reset_camera( hello_world_app_o *self ); // ffdecl.

// ----------------------------------------------------------------------
// FIXME: miplevels parameter placement is weird.
static bool initialiseImage( Image &img, char const *path, uint32_t mipLevels = 1, le_pixels_info::TYPE const &pixelType = le_pixels_info::eUInt8, le::Format const &imgFormat = le::Format::eR8G8B8A8Unorm, int numChannels = 4 ) {
	using namespace le_pixels;
	img.pixels     = le_pixels_i.create( path, numChannels, pixelType );
	img.pixelsInfo = le_pixels_i.get_info( img.pixels );

	// store earth albedo image handle
	img.imageHandle = LE_IMG_RESOURCE( path );
	img.imageInfo   = le::ImageInfoBuilder()
	                    .setFormat( imgFormat )
	                    .setExtent( img.pixelsInfo.width, img.pixelsInfo.height )
	                    .addUsageFlags( LE_IMAGE_USAGE_TRANSFER_DST_BIT )
	                    .setMipLevels( mipLevels )
	                    .build();

	img.textureHandle = LE_TEX_RESOURCE( ( std::string( path ) + "_tex" ).c_str() );
	return true;
}

// ----------------------------------------------------------------------

static hello_world_app_o *hello_world_app_create() {
	auto app = new ( hello_world_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 1920 / 2 )
	    .setHeight( 1080 / 2 )
	    .setTitle( "Hello world" );

	// create a new window
	app->window.setup( settings );

	le_swapchain_vk_settings_t swapchainSettings;
	swapchainSettings.presentmode_hint = le::Swapchain::Presentmode::eFifo;
	swapchainSettings.imagecount_hint  = 3;

	le_backend_vk_settings_t backendCreateInfo;
	backendCreateInfo.requestedExtensions = pal::Window::getRequiredVkExtensions( &backendCreateInfo.numRequestedExtensions );
	backendCreateInfo.swapchain_settings  = &swapchainSettings;
	backendCreateInfo.pWindow             = app->window;

	app->backend.setup( &backendCreateInfo );

	app->renderer.setup( app->backend );

	// -- Declare graphics pipeline state objects

	{
		// Set up the camera
		reset_camera( app );

		// Set the camera controller pivot distance to 0 to make the camera rotate around its proper axes,
		// not the pivot's.
		// app->cameraController.setPivotDistance( 0 );
	}

	{
		// Generate geometry for earth sphere
		app->sphereGenerator.generateSphere( 6371, 120, 120 ); // earth radius given in km.

		size_t vertexCount;
		size_t indexCount;
		app->sphereGenerator.getData( vertexCount, indexCount ); // only fetch counts so we can calculate memory requirements for vertex buffer, index buffer

		app->worldGeometry.vertexDataByteCount = vertexCount * sizeof( float ) * ( 3 + 3 + 2 + 3 );
		app->worldGeometry.vertexCount         = vertexCount;
		app->worldGeometry.indexCount          = indexCount;
		app->worldGeometry.index_buffer_info   = le::BufferInfoBuilder()
		                                           .addUsageFlags( LE_BUFFER_USAGE_INDEX_BUFFER_BIT )
		                                           .setSize( uint32_t( indexCount * sizeof( uint16_t ) ) )
		                                           .build();
		app->worldGeometry.vertex_buffer_info = le::BufferInfoBuilder()
		                                            .addUsageFlags( LE_BUFFER_USAGE_VERTEX_BUFFER_BIT )
		                                            .setSize( uint32_t( app->worldGeometry.vertexDataByteCount ) )
		                                            .build();
	}

	// load pixels for earth albedo

	initialiseImage( app->imgEarthAlbedo, "./local_resources/images/world_winter.jpg", 10 );
	initialiseImage( app->imgEarthNight, "./local_resources/images/earth_city_lights_8192_rs.png", 10, le_pixels_info::TYPE::eUInt8, le::Format::eR8Unorm, 1 );
	initialiseImage( app->imgEarthClouds, "./local_resources/images/earth_clouds.jpg", 10 );
	initialiseImage( app->imgEarthNormals, "./local_resources/images/earthNormalMap_8k-sobel.png", 10, le_pixels_info::eUInt16, le::Format::eR16G16B16A16Unorm );

	// initialise app timer
	app->timeStamp = std::chrono::high_resolution_clock::now();

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( hello_world_app_o *self ) {
	self->camera.setViewport( {0, 0, float( self->window.getSurfaceWidth() ), float( self->window.getSurfaceHeight() ), 0.f, 1.f} );
	self->camera.setClipDistances( 10.f, 150000.f );
	self->camera.setFovRadians( glm::radians( 25.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, 30000}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	self->camera.setViewMatrix( reinterpret_cast<float const *>( &camMatrix ) );
}

// ----------------------------------------------------------------------

typedef bool ( *renderpass_setup )( le_renderpass_o *pRp, void *user_data );

static bool pass_resource_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPass{pRp};
	auto app = static_cast<hello_world_app_o *>( user_data );

	rp.useResource( app->imgEarthAlbedo.imageHandle, app->imgEarthAlbedo.imageInfo );
	rp.useResource( app->imgEarthNight.imageHandle, app->imgEarthNight.imageInfo );
	rp.useResource( app->imgEarthNormals.imageHandle, app->imgEarthNormals.imageInfo );
	rp.useResource( app->imgEarthClouds.imageHandle, app->imgEarthClouds.imageInfo );
	rp.useResource( app->worldGeometry.vertex_buffer_handle, app->worldGeometry.vertex_buffer_info );
	rp.useResource( app->worldGeometry.index_buffer_handle, app->worldGeometry.index_buffer_info );

	return true;
}

// ----------------------------------------------------------------------

static void pass_resource_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<hello_world_app_o *>( user_data );
	le::Encoder encoder{encoder_};

	{

		if ( false == app->worldGeometry.wasLoaded ) {

			// fetch sphere geometry
			auto &geom = app->worldGeometry;

			uint16_t *sphereIndices{};
			float *   sphereVertices{};
			float *   sphereNormals{};
			float *   sphereUvs{};
			size_t    numVertices{};
			size_t    numIndices{};
			float *   sphereTangents{};
			app->sphereGenerator.getData( numVertices, numIndices, &sphereVertices, &sphereNormals, &sphereUvs, &sphereIndices );
			size_t numTangents;
			app->sphereGenerator.getTangents( numTangents, &sphereTangents );
			uint32_t offset = 0;

			// upload vertex positions
			geom.buffer_offsets[ 0 ] = 0;
			encoder.writeToBuffer( geom.vertex_buffer_handle, offset, sphereVertices, numVertices * sizeof( float ) * 3 );
			offset += numVertices * sizeof( float ) * 3;

			// upload vertex normals
			geom.buffer_offsets[ 1 ] = offset;
			encoder.writeToBuffer( geom.vertex_buffer_handle, offset, sphereNormals, numVertices * sizeof( float ) * 3 );
			offset += numVertices * sizeof( float ) * 3;

			// upload vertex uvs
			geom.buffer_offsets[ 2 ] = offset;
			encoder.writeToBuffer( geom.vertex_buffer_handle, offset, sphereUvs, numVertices * sizeof( float ) * 2 );
			offset += numVertices * sizeof( float ) * 2;

			// upload vertex tangents
			geom.buffer_offsets[ 3 ] = offset;
			encoder.writeToBuffer( geom.vertex_buffer_handle, offset, sphereTangents, numTangents * sizeof( float ) * 3 );
			offset += numVertices * sizeof( float ) * 3;

			// upload indices
			encoder.writeToBuffer( geom.index_buffer_handle, 0, sphereIndices, numIndices * sizeof( uint16_t ) );

			geom.wasLoaded = true;
		}

		auto uploadImage = [&]( Image &img ) {
			if ( false == img.wasLoaded ) {
				using namespace le_pixels;
				auto pixelsData = le_pixels_i.get_data( img.pixels );

				encoder.writeToImage( img.imageHandle,
				                      img.imageInfo,
				                      pixelsData,
				                      img.pixelsInfo.byte_count );

				le_pixels_i.destroy( img.pixels ); // Free pixels memory
				img.pixels = nullptr;              // Mark pixels memory as freed, otherwise Image.destroy() will double-free!

				img.wasLoaded = true;
			}
		};

		uploadImage( app->imgEarthAlbedo );
		uploadImage( app->imgEarthNormals );
		uploadImage( app->imgEarthNight );
		uploadImage( app->imgEarthClouds );
	}
}

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPass{pRp};
	auto app = static_cast<hello_world_app_o *>( user_data );

	auto texInfoAlbedo =
	    le::TextureInfoBuilder()
	        .withImageViewInfo()
	        .setImage( app->imgEarthAlbedo.imageHandle )
	        .end()
	        .withSamplerInfo()
	        .setAddressModeU( le::SamplerAddressMode::eRepeat )
	        .setAddressModeV( le::SamplerAddressMode::eMirroredRepeat )
	        .setMaxLod( 10.f )
	        .end()
	        .build();

	auto texInfoNight =
	    le::TextureInfoBuilder()
	        .withImageViewInfo()
	        .setImage( app->imgEarthNight.imageHandle )
	        .end()
	        .withSamplerInfo()
	        .setAddressModeU( le::SamplerAddressMode::eRepeat )
	        .setAddressModeV( le::SamplerAddressMode::eMirroredRepeat )
	        .setMaxLod( 10.f )
	        .end()
	        .build();

	auto texInfoClouds =
	    le::TextureInfoBuilder()
	        .withImageViewInfo()
	        .setImage( app->imgEarthClouds.imageHandle )
	        .end()
	        .withSamplerInfo()
	        .setAddressModeU( le::SamplerAddressMode::eRepeat )
	        .setAddressModeV( le::SamplerAddressMode::eMirroredRepeat )
	        .setMaxLod( 10.f )
	        .end()
	        .build();

	auto texInfoNormals =
	    le::TextureInfoBuilder()
	        .withImageViewInfo()
	        .setImage( app->imgEarthNormals.imageHandle )
	        .end()
	        .withSamplerInfo()
	        .setAddressModeU( le::SamplerAddressMode::eRepeat )
	        .setAddressModeV( le::SamplerAddressMode::eClampToEdge )
	        .setMaxLod( 10.f )
	        .end()
	        .build();

	rp
	    .addColorAttachment( app->renderer.getBackbufferResource() ) // color attachment
	    .addDepthStencilAttachment( LE_IMG_RESOURCE( "DEPTH_BUFFER" ) )
	    .sampleTexture( app->imgEarthAlbedo.textureHandle, texInfoAlbedo )
	    .sampleTexture( app->imgEarthNight.textureHandle, texInfoNight )
	    .sampleTexture( app->imgEarthNormals.textureHandle, texInfoNormals )
	    .sampleTexture( app->imgEarthClouds.textureHandle, texInfoClouds )
	    .useResource( app->worldGeometry.vertex_buffer_handle, app->worldGeometry.vertex_buffer_info )
	    .useResource( app->worldGeometry.index_buffer_handle, app->worldGeometry.index_buffer_info )
	    .setIsRoot( true ) //
	    ;

	return true;
}

// ----------------------------------------------------------------------

// type, triggerpointOnAxis, positionOnAxis, radius
static glm::vec4 lensflareData[] = {
    {3, 0.0, 0.0, 0.100}, //< flare point
    {0, 0.1, 0.1, 0.200},
    {0, 0.9, 0.9, 0.120},
    {0, 1.0, 1.0, 0.300},
    {0, 1.2, 1.2, 0.120},
    {0, 1.5, 1.5, 0.300},
    {1, 0.3, 0.3, 0.650},
    {1, 0.5, 0.5, 0.300}, ///< screen centre
    {1, 1.1, 1.1, 1.300},
    {1, 2.5, 2.5, 2.300},
    {2, 1.0, 1.0, 0.500},
    {2, 1.0, 1.1, 0.400},
    {2, 1.0, 1.2, 0.400},
    {2, 1.0, 1.5, 0.500},
    {2, 1.0, 2.5, 0.400},
};

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<hello_world_app_o *>( user_data );
	le::Encoder encoder{encoder_};

	auto screenWidth  = app->window.getSurfaceWidth();
	auto screenHeight = app->window.getSurfaceHeight();

	le::Viewport viewports[ 1 ] = {
	    {0.f, 0.f, float( screenWidth ), float( screenHeight ), 0.f, 1.f},
	};

	app->camera.setViewport( viewports[ 0 ] );

	le::Rect2D scissors[ 1 ] = {
	    {0, 0, screenWidth, screenHeight},
	};

	struct CameraParams {
		glm::mat4 view;
		glm::mat4 projection;
	};

	struct ModelParams {
		__attribute__( ( aligned( 16 ) ) ) glm::mat4 model;
		__attribute__( ( aligned( 16 ) ) ) glm::vec4 sunInEyeSpace;
		__attribute__( ( aligned( 16 ) ) ) glm::vec4 worldCentreInEyeSpace;
	};

	// Draw main scene
	if ( true ) {

		CameraParams cameraParams;
		ModelParams  earthParams;

		double speed           = 0.01; // degrees per millisecond
		double angularDistance = app->animate ? app->timeDelta * speed : 0;
		app->earthRotation     = fmod( app->earthRotation + angularDistance, 360.0 );

		earthParams.model       = glm::mat4( 1.f );                                                                                  // identity matrix
		earthParams.model       = glm::rotate( earthParams.model, glm::radians( -23.4f ), glm::vec3{0, 0, 1} );                      // apply ecliptic
		earthParams.model       = glm::rotate( earthParams.model, glm::radians( float( app->earthRotation ) ), glm::vec3{0, 1, 0} ); // apply day/night rotation
		cameraParams.view       = reinterpret_cast<glm::mat4 const &>( *app->camera.getViewMatrix() );
		cameraParams.projection = reinterpret_cast<glm::mat4 const &>( *app->camera.getProjectionMatrix() );

		glm::vec4 sunInWorldSpace         = glm::vec4{1000000, 0, 1000000, 1.f};
		glm::vec4 sourceInCameraSpace     = cameraParams.view * sunInWorldSpace;
		glm::vec4 worldCentreInWorldSpace = glm::vec4{0, 0, 0, 1};
		glm::vec4 worldCentreInEyeSpace   = cameraParams.view * earthParams.model * worldCentreInWorldSpace;
		glm::vec4 sourceInClipSpace       = cameraParams.projection * sourceInCameraSpace;
		sourceInClipSpace                 = sourceInClipSpace / sourceInClipSpace.w; // Normalise

		earthParams.sunInEyeSpace         = sourceInCameraSpace;
		earthParams.worldCentreInEyeSpace = worldCentreInEyeSpace;

		bool inFrustum = app->camera.getSphereCentreInFrustum( &sourceInCameraSpace.x, 100 );

		// draw mesh

		static auto pipelineEarthAlbedo =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/earth_albedo.vert", le::ShaderStage::eVertex ) )
		        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/earth_albedo.frag", le::ShaderStage::eFragment ) )
		        .withRasterizationState()
		        .setPolygonMode( le::PolygonMode::eFill )
		        .setCullMode( le::CullModeFlagBits::eBack )
		        .setFrontFace( le::FrontFace::eCounterClockwise )
		        .end()
		        .withInputAssemblyState()
		        .setToplogy( le::PrimitiveTopology::eTriangleList )
		        .end()
		        .withDepthStencilState()
		        .setDepthTestEnable( true )
		        .end()
		        .build();

		// We use the same buffer for the whole mesh, but at different offsets.
		// offsets are held by app->worldGeometry.buffer_offsets
		le_resource_handle_t buffers[ 4 ] = {
		    app->worldGeometry.vertex_buffer_handle, // position
		    app->worldGeometry.vertex_buffer_handle, // normal
		    app->worldGeometry.vertex_buffer_handle, // uv
		    app->worldGeometry.vertex_buffer_handle, // tangents
		};

		encoder
		    .setScissors( 0, 1, scissors )
		    .setViewports( 0, 1, viewports )
		    .bindGraphicsPipeline( pipelineEarthAlbedo )
		    .bindVertexBuffers( 0, 4, buffers, app->worldGeometry.buffer_offsets.data() )
		    .bindIndexBuffer( app->worldGeometry.index_buffer_handle, 0 );

		encoder
		    .setArgumentData( LE_ARGUMENT_NAME( "CameraParams" ), &cameraParams, sizeof( CameraParams ) )
		    .setArgumentData( LE_ARGUMENT_NAME( "ModelParams" ), &earthParams, sizeof( ModelParams ) )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_0" ), app->imgEarthAlbedo.textureHandle )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_1" ), app->imgEarthNormals.textureHandle )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_2" ), app->imgEarthNight.textureHandle )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_clouds" ), app->imgEarthClouds.textureHandle )
		    .drawIndexed( uint32_t( app->worldGeometry.indexCount ) ) //
		    ;

		// draw atmosphere

		static auto pipelineEarthAtmosphere =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/earth_atmosphere.vert", le::ShaderStage::eVertex ) )
		        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/earth_atmosphere.frag", le::ShaderStage::eFragment ) )
		        .withRasterizationState()
		        .setPolygonMode( le::PolygonMode::eFill )
		        .setCullMode( le::CullModeFlagBits::eBack )
		        .setFrontFace( le::FrontFace::eCounterClockwise )
		        .end()
		        .withAttachmentBlendState()
		        .usePreset( le::AttachmentBlendPreset::eAdd )
		        .end()
		        .withDepthStencilState()
		        .setDepthTestEnable( true )
		        .setDepthWriteEnable( false )
		        .end()
		        .build();

		earthParams.model = glm::scale( earthParams.model, glm::vec3{1.025f} );

		encoder
		    .bindGraphicsPipeline( pipelineEarthAtmosphere )
		    .setArgumentData( LE_ARGUMENT_NAME( "ModelParams" ), &earthParams, sizeof( ModelParams ) )
		    .setArgumentData( LE_ARGUMENT_NAME( "CameraParams" ), &cameraParams, sizeof( CameraParams ) )
		    .bindVertexBuffers( 0, 3, buffers, app->worldGeometry.buffer_offsets.data() )
		    .drawIndexed( uint32_t( app->worldGeometry.indexCount ) ) // index buffers should still be bound.
		    ;

		// let's check if source is in clip space

		if ( inFrustum ) {

			struct LensflareParams {
				// uCanvas:
				// .x -> global canvas height (in pixels)
				// .y -> global canvas width (in pixels)
				// .z -> identity distance, that is the distance at which canvas is rendered 1:1
				__attribute__( ( aligned( 16 ) ) ) glm::vec3 uCanvas;
				__attribute__( ( aligned( 16 ) ) ) glm::vec3 uLensflareSource; ///< source of flare in screen space
				float                                        uHowClose;
			};

			static auto pipelineLensflares =
			    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
			        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/lensflare.vert", le::ShaderStage::eVertex ) )
			        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/lensflare.frag", le::ShaderStage::eFragment ) )
			        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/lensflare.geom", le::ShaderStage::eGeometry ) )
			        .withRasterizationState()
			        .setPolygonMode( le::PolygonMode::eFill )
			        .setCullMode( le::CullModeFlagBits::eNone )
			        .end()
			        .withInputAssemblyState()
			        .setToplogy( le::PrimitiveTopology::ePointList )
			        .end()
			        .withAttachmentBlendState( 0 )
			        .usePreset( le::AttachmentBlendPreset::eAdd )
			        .end()
			        .withDepthStencilState()
			        .setDepthTestEnable( false )
			        .end()
			        .build();

			LensflareParams params{};
			params.uCanvas.x        = screenWidth * 0.25f;
			params.uCanvas.y        = screenHeight * 0.25f;
			params.uCanvas.z        = app->camera.getUnitDistance();
			params.uLensflareSource = sourceInClipSpace;
			params.uHowClose        = 500;

			encoder
			    .bindGraphicsPipeline( pipelineLensflares )
			    .setArgumentData( LE_ARGUMENT_NAME( "CameraParams" ), &cameraParams, sizeof( CameraParams ) )
			    .setArgumentData( LE_ARGUMENT_NAME( "LensflareParams" ), &params, sizeof( LensflareParams ) )
			    .setVertexData( lensflareData, sizeof( lensflareData ), 0 )
			    .draw( sizeof( lensflareData ) / sizeof( glm::vec4 ) ) //
			    ;
		} // end inFrustum
	}     // end draw main scene
}
// ----------------------------------------------------------------------

static bool hello_world_app_update( hello_world_app_o *self ) {

	static bool resetCameraOnReload = false; // reload meand module reload

	// Polls events for all windows -
	// This means any window may trigger callbacks for any events they have callbacks registered.
	pal::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	self->cameraController.setControlRect( 0, 0, float( self->window.getSurfaceWidth() ), float( self->window.getSurfaceHeight() ) );

	hello_world_app_process_ui_events( self );

	if ( resetCameraOnReload ) {
		// Reset camera
		reset_camera( self );
		resetCameraOnReload = false;
	}
	//	self->cameraController.setPivotDistance( 0 );

	auto now        = std::chrono::high_resolution_clock::now();
	self->timeDelta = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>( self->timeStamp - now ).count();
	self->timeStamp = now;

	le::RenderModule mainModule{};
	{
		le::RenderPass resourcePass( "resources", LE_RENDER_PASS_TYPE_TRANSFER );
		resourcePass.setSetupCallback( self, pass_resource_setup );
		resourcePass.setExecuteCallback( self, pass_resource_exec );

		le::RenderPass renderPassFinal( "root", LE_RENDER_PASS_TYPE_DRAW );
		renderPassFinal.setSetupCallback( self, pass_main_setup );
		renderPassFinal.setExecuteCallback( self, pass_main_exec );

		mainModule.addRenderPass( resourcePass );
		mainModule.addRenderPass( renderPassFinal );
	}

	// Update will call all rendercallbacks in this module.
	// the RECORD phase is guaranteed to execute - all rendercallbacks will get called.
	self->renderer.update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------
static void hello_world_app_process_ui_events( hello_world_app_o *self ) {
	using namespace pal_window;
	uint32_t         numEvents;
	LeUiEvent const *pEvents;
	window_i.get_ui_event_queue( self->window, &pEvents, numEvents );

	std::vector<LeUiEvent> events{pEvents, pEvents + numEvents};

	bool wantsToggle = false;

	for ( auto &event : events ) {
		switch ( event.event ) {
		case ( LeUiEvent::Type::eKey ): {
			auto &e = event.key;
			if ( e.action == LeUiEvent::ButtonAction::eRelease && e.key == LeUiEvent::NamedKey::eF11 ) {
				wantsToggle ^= true;
			} else if ( e.action == LeUiEvent::ButtonAction::eRelease && e.key == LeUiEvent::NamedKey::eZ ) {
				reset_camera( self );
				float distance_to_origin = glm::distance( glm::vec4{0, 0, 0, 1}, glm::inverse( *reinterpret_cast<glm::mat4 const *>( self->camera.getViewMatrix() ) ) * glm::vec4( 0, 0, 0, 1 ) );
				self->cameraController.setPivotDistance( distance_to_origin );
			} else if ( e.action == LeUiEvent::ButtonAction::eRelease && e.key == LeUiEvent::NamedKey::eX ) {
				self->cameraController.setPivotDistance( 0 );
			} else if ( e.action == LeUiEvent::ButtonAction::eRelease && e.key == LeUiEvent::NamedKey::eC ) {
				float distance_to_origin = glm::distance( glm::vec4{0, 0, 0, 1}, glm::inverse( *reinterpret_cast<glm::mat4 const *>( self->camera.getViewMatrix() ) ) * glm::vec4( 0, 0, 0, 1 ) );
				self->cameraController.setPivotDistance( distance_to_origin );
			} else if ( e.action == LeUiEvent::ButtonAction::eRelease && e.key == LeUiEvent::NamedKey::eA ) {
				self->animate ^= true;
			}

		} break;
		default:
			// do nothing
		    break;
		}
	}

	self->cameraController.processEvents( self->camera, events.data(), events.size() );

	if ( wantsToggle ) {
		self->window.toggleFullscreen();
	}
}

// ----------------------------------------------------------------------

static void hello_world_app_destroy( hello_world_app_o *self ) {

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_hello_world_app_api( void *api ) {
	auto  hello_world_app_api_i = static_cast<hello_world_app_api *>( api );
	auto &hello_world_app_i     = hello_world_app_api_i->hello_world_app_i;

	hello_world_app_i.initialize = initialize;
	hello_world_app_i.terminate  = terminate;

	hello_world_app_i.create  = hello_world_app_create;
	hello_world_app_i.destroy = hello_world_app_destroy;
	hello_world_app_i.update  = hello_world_app_update;
}
