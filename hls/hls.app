<project xmlns="com.autoesl.autopilot.project" name="hls" top="compute_correlation_hls" projectType="C/C++">
    <includePaths/>
    <libraryPaths/>
    <Simulation>
        <SimFlow name="csim" csimMode="0" lastCsimMode="0"/>
    </Simulation>
    <files xmlns="">
        <file name="src/quantum_kernels.cpp" sc="0" tb="false" cflags="-std=c++17" csimflags="" blackbox="false"/>
        <file name="../src/main.cpp" sc="0" tb="1" cflags="-I../../../include -std=c++17 -Wno-unknown-pragmas" csimflags="" blackbox="false"/>
        <file name="../../include/quantum_kernels.h" sc="0" tb="1" cflags="-Wno-unknown-pragmas" csimflags="" blackbox="false"/>
    </files>
    <solutions xmlns="">
        <solution name="solution1" status="active"/>
        <solution name="solution1" status="active"/>
    </solutions>
</project>

