import ErrorBoundary from "@/components/ErrorBoundary";
import Workspace from "@/components/Workspace";

export default function Page() {
  return (
    <ErrorBoundary>
      <Workspace />
    </ErrorBoundary>
  );
}
