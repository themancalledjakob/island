#include "triangle_app.h"

#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"

#include "le_camera/le_camera.h"
#include "le_pipeline_builder/le_pipeline_builder.h"

#define VULKAN_HPP_NO_SMART_HANDLE
#include "vulkan/vulkan.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h> // for key codes

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>

#define LE_ARGUMENT_NAME( x ) hash_64_fnv1a_const( #x )

struct le_mouse_event_data_o {
	uint32_t  buttonState{};
	glm::vec2 cursor_pos;
};

struct triangle_app_o {
	std::unique_ptr<le::Backend>  backend;
	std::unique_ptr<pal::Window>  window;
	std::unique_ptr<le::Renderer> renderer;
	uint64_t                      frame_counter = 0;

	LeCamera           camera;
	LeCameraController cameraController;
};

// ----------------------------------------------------------------------

static void initialize() {
	pal::Window::init();
};

// ----------------------------------------------------------------------

static void terminate() {
	pal::Window::terminate();
};

static void reset_camera( triangle_app_o *self ); // ffdecl.

// ----------------------------------------------------------------------

static triangle_app_o *triangle_app_create() {
	auto app = new ( triangle_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Hello world" );

	// create a new window
	app->window = std::make_unique<pal::Window>( settings );

	le_backend_vk_settings_t backendCreateInfo;
	backendCreateInfo.requestedExtensions = pal::Window::getRequiredVkExtensions( &backendCreateInfo.numRequestedExtensions );

	app->backend = std::make_unique<le::Backend>( &backendCreateInfo );

	// We need a valid instance at this point.
	app->backend->createWindowSurface( *app->window );

	le_swapchain_vk_settings_o swapchainSettings;
	swapchainSettings.presentmode_hint = le::Swapchain::Presentmode::eImmediate;
	swapchainSettings.imagecount_hint  = 2;

	app->backend->createSwapchain( &swapchainSettings ); // TODO (swapchain) - make it possible to set swapchain parameters

	app->backend->setup();

	app->renderer = std::make_unique<le::Renderer>( *app->backend );
	app->renderer->setup();

	// -- Declare graphics pipeline state objects

	{
		// set up the camera
		reset_camera( app );
	}

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( triangle_app_o *self ) {
	self->camera.setViewport( {0, 0, float( self->window->getSurfaceWidth() ), float( self->window->getSurfaceHeight() ), 0.f, 1.f} );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, self->camera.getUnitDistance()}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	self->camera.setViewMatrix( reinterpret_cast<float const *>( &camMatrix ) );
}

// ----------------------------------------------------------------------

typedef bool ( *renderpass_setup )( le_renderpass_o *pRp, void *user_data );

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPassRef{pRp};
	auto app = static_cast<triangle_app_o *>( user_data );

	rp
	    .addImageAttachment( app->renderer->getBackbufferResource() ) // color attachment
	    .setIsRoot( true );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<triangle_app_o *>( user_data );
	le::Encoder encoder{encoder_};

	auto screenWidth  = app->window->getSurfaceWidth();
	auto screenHeight = app->window->getSurfaceHeight();

	le::Viewport viewports[ 1 ] = {
	    {0.f, 0.f, float( screenWidth ), float( screenHeight ), 0.f, 1.f},
	};

	app->camera.setViewport( viewports[ 0 ] );

	le::Rect2D scissors[ 1 ] = {
	    {0, 0, screenWidth, screenHeight},
	};

	// data as it is laid out in the ubo for the shader
	struct ColorUbo_t {
		glm::vec4 color;
	};

	struct MatrixStackUbo_t {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	// Draw main scene
	if ( true ) {

		static auto shaderVert = app->renderer->createShaderModule( "./resources/shaders/default.vert", LeShaderType::eVert );
		static auto shaderFrag = app->renderer->createShaderModule( "./resources/shaders/default.frag", LeShaderType::eFrag );

		static auto pipelineTriangle =
		    LeGraphicsPipelineBuilder( *app->backend )
		        .setVertexShader( shaderVert )
		        .setFragmentShader( shaderFrag )
		        .build();

		MatrixStackUbo_t mvp;
		mvp.model      = glm::mat4( 1.f ); // identity matrix
		mvp.model      = glm::scale( mvp.model, glm::vec3( 4.5 ) );
		mvp.view       = reinterpret_cast<glm::mat4 const &>( *app->camera.getViewMatrix() );
		mvp.projection = *reinterpret_cast<glm::mat4 const *>( app->camera.getProjectionMatrix() );

		glm::vec3 trianglePositions[] = {
		    {-50, -50, 0},
		    {50, -50, 0},
		    {0, 50, 0},
		};

		glm::vec4 triangleColors[] = {
		    {1, 0, 0, 1.f},
		    {0, 1, 0, 1.f},
		    {0, 0, 1, 1.f},
		};

		encoder
		    .bindGraphicsPipeline( pipelineTriangle )
		    .setScissors( 0, 1, scissors )
		    .setViewports( 0, 1, viewports )
		    .setArgumentData( LE_ARGUMENT_NAME( MatrixStack ), &mvp, sizeof( MatrixStackUbo_t ) )
		    .setVertexData( trianglePositions, sizeof( trianglePositions ), 0 )
		    .setVertexData( triangleColors, sizeof( triangleColors ), 1 )
		    .draw( 3 );
	}
}

// ----------------------------------------------------------------------

static bool triangle_app_update( triangle_app_o *self ) {

	static bool resetCameraOnReload = true;

	// Polls events for all windows -
	// This means any window may trigger callbacks for any events they have callbacks registered.
	pal::Window::pollEvents();

	if ( self->window->shouldClose() ) {
		return false;
	}

	using namespace le_renderer;

	le::RenderModule mainModule{};
	{

		le::RenderPass renderPassFinal( "root", LE_RENDER_PASS_TYPE_DRAW );
		renderPassFinal.setSetupCallback( self, pass_main_setup );
		renderPassFinal.setExecuteCallback( self, pass_main_exec );

		mainModule.addRenderPass( renderPassFinal );
	}

	// Update will call all rendercallbacks in this module.
	// the RECORD phase is guaranteed to execute - all rendercallbacks will get called.
	self->renderer->update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void triangle_app_destroy( triangle_app_o *self ) {
	delete ( self );
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_triangle_app_api( void *api ) {
	auto  triangle_app_api_i = static_cast<triangle_app_api *>( api );
	auto &triangle_app_i     = triangle_app_api_i->triangle_app_i;

	triangle_app_i.initialize = initialize;
	triangle_app_i.terminate  = terminate;

	triangle_app_i.create  = triangle_app_create;
	triangle_app_i.destroy = triangle_app_destroy;
	triangle_app_i.update  = triangle_app_update;
}
