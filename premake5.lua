workspace "livetrace"
  configurations {"Debug", "Release"}
  platforms {"x64"}
  location "build"

project "livetrace"
  kind "WindowedApp"
  language "C++"
  cppdialect "C++latest"
  targetdir "build"
  nuget { 
    "Microsoft.Windows.ImplementationLibrary:1.0.230824.2",
    "Microsoft.Web.WebView2:1.0.1938.49"
  }
  files {
    "*.cc",
    "*.h",
  }
  includedirs { "./" }
  pchsource "pch.cc"
  pchheader "pch.h"
  forceincludes { "pch.h" }
  vpaths { ["*"] = "./" }
  defines {
    "NOMINMAX"
  }
  disablewarnings { 4554 }
  filter "configurations:Debug"
    defines { "DEBUG" }
    symbols "On"
  filter "configurations:Release"
    defines { "NDEBUG" }
    optimize "On"