<project xmlns="com.autoesl.autopilot.project" top="compute_correlation_hls" name="aurora_hls" projectType="C/C++">
    <includePaths/>
    <libraryPaths/>
    <Simulation>
        <SimFlow name="csim" csimMode="0" lastCsimMode="0"/>
    </Simulation>
    <files xmlns="">
        <file name="aurora_hls/src/quantum_kernels.cpp" sc="0" tb="false" cflags="-I" csimflags="" blackbox="false"/>
        <file name="../src/main.cpp" sc="0" tb="1" cflags="-I../.apc -I../.apc -Wno-unknown-pragmas" csimflags="" blackbox="false"/>
        <file name="../src/quantum_kernels.h" sc="0" tb="1" cflags="-Wno-unknown-pragmas" csimflags="" blackbox="false"/>
    </files>
    <solutions xmlns="">
        <solution name="solution1" status="active"/>
    </solutions>
</project>

