#include <sycl/sycl.hpp>

int main() {
   sycl::queue q(sycl::gpu_selector_v);

   auto device_name = q.get_device().get_info<sycl::info::device::name>();
   auto numSlices = q.get_device().get_info<sycl::ext::intel::info::device::gpu_slices>();
   auto numSubslicesPerSlice = q.get_device().get_info<sycl::ext::intel::info::device::gpu_subslices_per_slice>();
   auto numEUsPerSubslice = q.get_device().get_info<sycl::ext::intel::info::device::gpu_eu_count_per_subslice>();
   auto numThreadsPerEU = q.get_device().get_info<sycl::ext::intel::info::device::gpu_hw_threads_per_eu>();
   auto global_mem_size = q.get_device().get_info<sycl::info::device::global_mem_size>();
   auto local_mem_size = q.get_device().get_info<sycl::info::device::local_mem_size>();
   auto max_work_group_size = q.get_device().get_info<sycl::info::device::max_work_group_size>();
   auto sub_group_sizes = q.get_device().get_info<sycl::info::device::sub_group_sizes>();

   std::cout << "Intel GPU Characteristics:\n";
   std::cout << " GPU Model : " << device_name << "\n";
   std::cout << " XeCore count : " << numSlices * numSubslicesPerSlice << "\n";
   std::cout << " Vector Engines per XeCore : " << numEUsPerSubslice << "\n";
   std::cout << " Vector Engine count : " << numSlices * numSubslicesPerSlice * numEUsPerSubslice << "\n";
   std::cout << " Hardware Threads per Vector Engine : " << numThreadsPerEU << "\n";
   std::cout << " Hardware Threads count : " << numSlices * numSubslicesPerSlice * numEUsPerSubslice * numThreadsPerEU << "\n";
   std::cout << " GPU Memory Size : " << global_mem_size << "\n";
   std::cout << " Shared Local Memory per Work-group : " << local_mem_size << "\n";
   std::cout << " Max Work-group size : " << max_work_group_size << "\n";
   std::cout << " Supported Sub-group sizes : ";
   for (int i=0; i<sub_group_sizes.size(); i++) std::cout << sub_group_sizes[i] << " "; std::cout << "\n";
}
