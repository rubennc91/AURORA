<project xmlns="com.autoesl.autopilot.project" name="hls_sift" top="compute_correlation_hls" projectType="C/C++">
    <includePaths/>
    <libraryPaths/>
    <Simulation>
        <SimFlow name="csim" csimMode="0" lastCsimMode="0"/>
    </Simulation>
    <files xmlns="">
        <file name="hls_sift/srcs/quantum_kernels.cpp" sc="0" tb="false" cflags="" csimflags="" blackbox="false"/>
        <file name="hls_sift/srcs/quantum_kernels.h" sc="0" tb="false" cflags="" csimflags="" blackbox="false"/>
        <file name="../srcs/main.cpp" sc="0" tb="1" cflags="-Wno-unknown-pragmas" csimflags="" blackbox="false"/>
    </files>
    <solutions xmlns="">
        <solution name="solution1" status="active"/>
    </solutions>
</project>

