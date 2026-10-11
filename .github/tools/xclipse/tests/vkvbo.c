/* Vertex buffer test: like vktri, but positions (R32G32_SFLOAT) and colors (R8G8B8A8_UNORM)
 * come from an interleaved vertex buffer, as in WebGL/PIXI. Checks the pixels near each corner.
 * DYN=1 sets the vertex input with VK_EXT_vertex_input_dynamic_state (what Zink uses).
 * CLEANUP=1 destroys every object and the device before exiting. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "vbo_vert.h"
#include "tri_frag.h"

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s -> %d\n", #x, r_); return 1; } } while (0)
#define W 64
#define H 64

void ibdump_install(const char *log_path);

static uint32_t find_mem(VkPhysicalDevice pd, uint32_t bits, VkMemoryPropertyFlags want)
{
   VkPhysicalDeviceMemoryProperties mp;
   vkGetPhysicalDeviceMemoryProperties(pd, &mp);
   for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
      if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
   return 0;
}

int main(void)
{
#ifdef WITH_IBDUMP
   if (getenv("IBDUMP")) ibdump_install(getenv("IBDUMP"));
#endif
   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
   VkInstance inst;
   CHECK(vkCreateInstance(&ici, NULL, &inst));
   uint32_t n = 1;
   VkPhysicalDevice pd;
   vkEnumeratePhysicalDevices(inst, &n, &pd);
   float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1,
                                  .pQueuePriorities = &prio};
   const int dyn = getenv("DYN") != NULL;
   const char *dyn_ext = VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME;
   VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT dynf = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT,
      .vertexInputDynamicState = VK_TRUE};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qci};
   if (dyn) {
      dci.pNext = &dynf;
      dci.enabledExtensionCount = 1;
      dci.ppEnabledExtensionNames = &dyn_ext;
   }
   VkDevice dev;
   VkResult dr = vkCreateDevice(pd, &dci, NULL, &dev);
   if (dyn && dr == VK_ERROR_EXTENSION_NOT_PRESENT) {
      printf("NOT SUPPORTED: %s\n", dyn_ext);
      return 0;
   }
   CHECK(dr);
   VkQueue q;
   vkGetDeviceQueue(dev, 0, 0, &q);

   /* Color image */
   VkImageCreateInfo imci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                             .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {W, H, 1}, .mipLevels = 1,
                             .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                             .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
   VkImage img;
   CHECK(vkCreateImage(dev, &imci, NULL, &img));
   VkMemoryRequirements mr;
   vkGetImageMemoryRequirements(dev, img, &mr);
   VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                               .memoryTypeIndex = find_mem(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
   VkDeviceMemory imem;
   CHECK(vkAllocateMemory(dev, &mai, NULL, &imem));
   CHECK(vkBindImageMemory(dev, img, imem, 0));
   VkImageViewCreateInfo ivci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
                                 .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
                                 .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
   VkImageView view;
   CHECK(vkCreateImageView(dev, &ivci, NULL, &view));

   /* Readback buffer */
   VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = W * H * 4,
                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
   VkBuffer buf;
   CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
   vkGetBufferMemoryRequirements(dev, buf, &mr);
   mai.allocationSize = mr.size;
   mai.memoryTypeIndex = find_mem(pd, mr.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   VkDeviceMemory bmem;
   CHECK(vkAllocateMemory(dev, &mai, NULL, &bmem));
   CHECK(vkBindBufferMemory(dev, buf, bmem, 0));
   uint8_t *px;
   CHECK(vkMapMemory(dev, bmem, 0, W * H * 4, 0, (void **)&px));
   memset(px, 0x55, W * H * 4);

   /* Interleaved vertex buffer: vec2 position + RGBA8 color, 12 bytes per vertex. */
   struct vtx { float x, y; uint8_t c[4]; } verts[3] = {
      {-0.8f, -0.8f, {255, 0, 0, 255}}, {0.8f, -0.8f, {0, 255, 0, 255}}, {0.0f, 0.8f, {0, 0, 255, 255}}};
   VkBufferCreateInfo vbci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(verts),
                              .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
   VkBuffer vbuf;
   CHECK(vkCreateBuffer(dev, &vbci, NULL, &vbuf));
   vkGetBufferMemoryRequirements(dev, vbuf, &mr);
   mai.allocationSize = mr.size;
   mai.memoryTypeIndex = find_mem(pd, mr.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   VkDeviceMemory vmem;
   CHECK(vkAllocateMemory(dev, &mai, NULL, &vmem));
   CHECK(vkBindBufferMemory(dev, vbuf, vmem, 0));
   void *vmap;
   CHECK(vkMapMemory(dev, vmem, 0, sizeof(verts), 0, &vmap));
   memcpy(vmap, verts, sizeof(verts));

   /* Render pass + framebuffer */
   VkAttachmentDescription att = {.format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT,
                                  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                  .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                  .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                  .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                  .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
   VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
   VkSubpassDescription sub = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1,
                               .pColorAttachments = &ref};
   VkSubpassDependency depn = {.srcSubpass = 0, .dstSubpass = VK_SUBPASS_EXTERNAL,
                               .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
                               .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                               .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
   VkRenderPassCreateInfo rpci = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1,
                                  .pAttachments = &att, .subpassCount = 1, .pSubpasses = &sub,
                                  .dependencyCount = 1, .pDependencies = &depn};
   VkRenderPass rp;
   CHECK(vkCreateRenderPass(dev, &rpci, NULL, &rp));
   VkFramebufferCreateInfo fbci = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = rp,
                                   .attachmentCount = 1, .pAttachments = &view, .width = W, .height = H,
                                   .layers = 1};
   VkFramebuffer fb;
   CHECK(vkCreateFramebuffer(dev, &fbci, NULL, &fb));

   /* Pipeline */
   VkShaderModuleCreateInfo smci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
   VkShaderModule vs, fs;
   smci.codeSize = sizeof(vbo_vert_spv);
   smci.pCode = vbo_vert_spv;
   CHECK(vkCreateShaderModule(dev, &smci, NULL, &vs));
   smci.codeSize = sizeof(tri_frag_spv);
   smci.pCode = tri_frag_spv;
   CHECK(vkCreateShaderModule(dev, &smci, NULL, &fs));
   VkPipelineShaderStageCreateInfo stages[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vs, .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fs, .pName = "main"}};
   VkVertexInputBindingDescription vb = {0, sizeof(struct vtx), VK_VERTEX_INPUT_RATE_VERTEX};
   VkVertexInputAttributeDescription va[2] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                                              {1, 0, VK_FORMAT_R8G8B8A8_UNORM, 8}};
   VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                                              .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vb,
                                              .vertexAttributeDescriptionCount = 2,
                                              .pVertexAttributeDescriptions = va};
   VkDynamicState dyn_state = VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;
   VkPipelineDynamicStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                          .dynamicStateCount = 1, .pDynamicStates = &dyn_state};
   VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
   VkViewport vp = {0, 0, W, H, 0, 1};
   VkRect2D sc = {{0, 0}, {W, H}};
   VkPipelineViewportStateCreateInfo vps = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                            .viewportCount = 1, .pViewports = &vp, .scissorCount = 1,
                                            .pScissors = &sc};
   VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
                                                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f};
   VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                              .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
   VkPipelineColorBlendAttachmentState cba = {.colorWriteMask = 0xf};
   VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                             .attachmentCount = 1, .pAttachments = &cba};
   VkPipelineLayoutCreateInfo plci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
   VkPipelineLayout pl;
   CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
   VkGraphicsPipelineCreateInfo gpci = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2,
                                        .pStages = stages, .pVertexInputState = &vi, .pInputAssemblyState = &ia,
                                        .pViewportState = &vps, .pRasterizationState = &rs,
                                        .pMultisampleState = &ms, .pColorBlendState = &cb, .layout = pl,
                                        .renderPass = rp, .pDynamicState = dyn ? &ds : NULL};
   VkPipeline pipe;
   CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe));

   /* Record */
   VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
   VkCommandPool pool;
   CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));
   VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
   VkCommandBuffer cmd;
   CHECK(vkAllocateCommandBuffers(dev, &cai, &cmd));
   VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   CHECK(vkBeginCommandBuffer(cmd, &cbi));
   VkClearValue clear = {.color = {{0, 0, 0, 1}}};
   VkRenderPassBeginInfo rpbi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = rp,
                                 .framebuffer = fb, .renderArea = {{0, 0}, {W, H}}, .clearValueCount = 1,
                                 .pClearValues = &clear};
   vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
   if (!getenv("NODRAW")) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      if (dyn) {
         PFN_vkCmdSetVertexInputEXT set_vi =
            (PFN_vkCmdSetVertexInputEXT)vkGetDeviceProcAddr(dev, "vkCmdSetVertexInputEXT");
         VkVertexInputBindingDescription2EXT vb2 = {.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
                                                    .binding = 0, .stride = sizeof(struct vtx),
                                                    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX, .divisor = 1};
         VkVertexInputAttributeDescription2EXT va2[2] = {
            {VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, NULL, 0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
            {VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, NULL, 1, 0, VK_FORMAT_R8G8B8A8_UNORM, 8}};
         set_vi(cmd, 1, &vb2, 2, va2);
      }
      VkDeviceSize vb_off = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &vb_off);
      vkCmdDraw(cmd, 3, 1, 0, 0);
   }
   vkCmdEndRenderPass(cmd);
   VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {W, H, 1}};
   vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &region);
   VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                         .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
   CHECK(vkEndCommandBuffer(cmd));

   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   CHECK(vkCreateFence(dev, &fci, NULL, &fence));
   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
   CHECK(vkQueueSubmit(q, 1, &si, fence));
   VkResult r = vkWaitForFences(dev, 1, &fence, VK_TRUE, 5ull * 1000 * 1000 * 1000);
   printf("wait=%s\n", r == VK_SUCCESS ? "signaled" : r == VK_TIMEOUT ? "TIMEOUT" : "error");

   /* Corner pixels: (-0.8,-0.8) is red at the top left, (0.8,-0.8) green at the top right,
    * (0,0.8) blue at the bottom; (2,2) is outside and keeps the black clear color. */
   const int probe[][2] = {{2, 2}, {10, 8}, {54, 8}, {32, 54}, {32, 30}};
   const int want[] = {-1, 0, 1, 2, 3}; /* dominant channel, -1 = black, 3 = any color */
   int ok = r == VK_SUCCESS;
   for (unsigned i = 0; i < 5; i++) {
      const uint8_t *p = px + (probe[i][1] * W + probe[i][0]) * 4;
      int maxc = p[0] >= p[1] && p[0] >= p[2] ? 0 : p[1] >= p[2] ? 1 : 2;
      int good = want[i] == -1 ? (p[0] + p[1] + p[2] < 16) : want[i] == 3 ? (p[0] + p[1] + p[2] > 64)
                                                                           : (p[maxc] > 128 && maxc == want[i]);
      ok &= good;
      printf("pixel(%2d,%2d) = %3u %3u %3u %3u %s\n", probe[i][0], probe[i][1], p[0], p[1], p[2], p[3],
             good ? "ok" : "WRONG");
   }
   printf("%s (%s vertex input)\n", ok ? "PASS" : "FAIL", dyn ? "dynamic" : "static");
   FILE *f = fopen("vbo.ppm", "wb");
   if (f) {
      fprintf(f, "P6\n%d %d\n255\n", W, H);
      for (int i = 0; i < W * H; i++) fwrite(px + i * 4, 1, 3, f);
      fclose(f);
   }
   if (getenv("CLEANUP")) {
      /* Tear everything down explicitly instead of leaving it to the kernel at exit. */
      vkDeviceWaitIdle(dev);
      vkDestroyFence(dev, fence, NULL);
      vkDestroyCommandPool(dev, pool, NULL);
      vkDestroyPipeline(dev, pipe, NULL);
      vkDestroyPipelineLayout(dev, pl, NULL);
      vkDestroyShaderModule(dev, vs, NULL);
      vkDestroyShaderModule(dev, fs, NULL);
      vkDestroyFramebuffer(dev, fb, NULL);
      vkDestroyRenderPass(dev, rp, NULL);
      vkDestroyBuffer(dev, vbuf, NULL);
      vkFreeMemory(dev, vmem, NULL);
      vkDestroyBuffer(dev, buf, NULL);
      vkFreeMemory(dev, bmem, NULL);
      vkDestroyImageView(dev, view, NULL);
      vkDestroyImage(dev, img, NULL);
      vkFreeMemory(dev, imem, NULL);
      vkDestroyDevice(dev, NULL);
      vkDestroyInstance(inst, NULL);
   }
   return !ok;
}
